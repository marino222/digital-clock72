#pragma once

#include <Arduino.h>

// Frame-timing utility for the Phase 1.1 bring-up test. Times whole
// frames (render + full SPI transfer) with micros() and reports at
// most once per interval, since per-frame Serial prints would skew
// the very number being measured.
class FpsCounter {
 public:
  // Call once per loop, immediately before the frame's rendering work.
  void beginFrame();

  // Call once per loop, after the frame (including pushSprite()) has
  // fully completed.
  void endFrame();

  // Prints an averaged FPS line to Serial at most once per intervalMs.
  void maybeReport(uint32_t intervalMs = 1000);

  // Blocks the caller's rendering loop for exactly windowMs (via the
  // supplied renderOneFrame callback), then prints a single summary
  // line: "<label>: N frames / X.Xs / Y.Y avg FPS".
  template <typename RenderOneFrame>
  void runBenchmarkWindow(uint32_t windowMs, const char* label, RenderOneFrame renderOneFrame) {
    uint32_t frames = 0;
    uint32_t windowStartUs = micros();
    while (micros() - windowStartUs < windowMs * 1000UL) {
      renderOneFrame();
      ++frames;
    }
    float seconds = (micros() - windowStartUs) / 1'000'000.0f;
    Serial.printf("%s: %lu frames / %.2fs / %.1f avg FPS\n", label,
                  static_cast<unsigned long>(frames), seconds, frames / seconds);
  }

 private:
  uint32_t _frameStartUs = 0;
  uint32_t _windowFrames = 0;
  uint32_t _windowMinUs = UINT32_MAX;
  uint32_t _windowMaxUs = 0;
  uint32_t _windowStartMs = 0;
};
