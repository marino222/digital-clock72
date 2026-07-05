#include "angle_source.h"

#include <cmath>

void AngleSource::begin() {
  _lastSweepStepMs = millis();
  _serialBuffer.reserve(16);
}

void AngleSource::pollSerial() {
  while (Serial.available() > 0) {
    char c = static_cast<char>(Serial.read());
    if (c == '\n' || c == '\r') {
      if (_serialBuffer.length() > 1 &&
          (_serialBuffer[0] == 'A' || _serialBuffer[0] == 'a')) {
        float angle = _serialBuffer.substring(1).toFloat();
        _overrideAngle = fmodf(angle, 360.0f);
        if (_overrideAngle < 0) _overrideAngle += 360.0f;
        _pendingOverride = true;
        _mode = Mode::SERIAL_OVERRIDE;
      }
      _serialBuffer = "";
    } else {
      _serialBuffer += c;
    }
  }
}

bool AngleSource::poll(float& outTargetAngleDeg) {
  pollSerial();

  if (_pendingOverride) {
    _pendingOverride = false;
    outTargetAngleDeg = _overrideAngle;
    return true;
  }

  if (_mode != Mode::AUTO_SWEEP) return false;

  uint32_t now = millis();
  if (now - _lastSweepStepMs < _sweepIntervalMs) return false;

  _lastSweepStepMs = now;
  _sweepAngle = fmodf(_sweepAngle + 30.0f, 360.0f);
  outTargetAngleDeg = _sweepAngle;
  return true;
}
