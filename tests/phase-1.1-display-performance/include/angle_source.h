#pragma once


#include <Arduino.h>
#include <cstdint>

// Stand-in for "an instruction arrived over RS485". This is the seam
// that phase 2/3's real RS485 decoding will replace -- ClockFace and
// main.cpp's loop shape do not need to change when that happens, only
// this class's implementation.
//
// Two ways to drive it right now:
//  - AUTO_SWEEP: issues a new target angle every ~1-2s on its own, to
//    continuously exercise the easing path for the FPS benchmark.
//  - Typing "A<degrees>" + Enter on the Serial Monitor (e.g. "A135")
//    overrides it with an explicit angle -- the primitive clock
//    control interface.
class AngleSource {
 public:
  void begin();

  // Call once per loop. Returns true and fills outTargetAngleDeg if a
  // new target angle is available this call.
  bool poll(float& outTargetAngleDeg);

 private:
  void pollSerial();

  enum class Mode { AUTO_SWEEP, SERIAL_OVERRIDE } _mode = Mode::AUTO_SWEEP;

  float _sweepAngle = 0.0f;
  uint32_t _lastSweepStepMs = 0;
  uint32_t _sweepIntervalMs = 1500;

  bool _pendingOverride = false;
  float _overrideAngle = 0.0f;
  String _serialBuffer;
};
