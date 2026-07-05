#include "fps_counter.h"

void FpsCounter::beginFrame() { _frameStartUs = micros(); }

void FpsCounter::endFrame() {
  if (_windowFrames == 0) _windowStartMs = millis();

  uint32_t frameUs = micros() - _frameStartUs;
  _windowMinUs = min(_windowMinUs, frameUs);
  _windowMaxUs = max(_windowMaxUs, frameUs);
  ++_windowFrames;
}

void FpsCounter::maybeReport(uint32_t intervalMs) {
  uint32_t now = millis();
  if (_windowFrames == 0 || now - _windowStartMs < intervalMs) return;

  float seconds = (now - _windowStartMs) / 1000.0f;
  float avgFps = _windowFrames / seconds;
  Serial.printf("fps: %.1f avg (min %.1f / max %.1f) over %lu frames / %.2fs\n", avgFps,
                1'000'000.0f / _windowMaxUs, 1'000'000.0f / _windowMinUs,
                static_cast<unsigned long>(_windowFrames), seconds);

  _windowFrames = 0;
  _windowMinUs = UINT32_MAX;
  _windowMaxUs = 0;
}
