#pragma once

#include <LovyanGFX.hpp>

// Draws a round clock face with a single animated hand and eases the
// hand between angles instead of jumping. This is the part meant to
// carry into slave/ largely unchanged -- everything it needs (the
// display and a target angle) will eventually come from a decoded
// RS485 command instead of angle_source.h.
class ClockFace {
 public:
  explicit ClockFace(lgfx::LGFX_Device& display);

  // Allocates the double-buffer sprite.
  void begin();

  // The "instruction" entry point -- what a decoded RS485 command will
  // call in the real slave firmware.
  void setTargetAngle(float degrees);

  // Advances the current angle toward the target with easing.
  // Frame-rate independent: pass the elapsed time since the last call.
  void update(uint32_t dtMicros);

  // Redraws face + hand into the backbuffer at the current angle and
  // pushes the full frame to the display.
  void render();

  float currentAngle() const { return _currentAngle; }

 private:
  static float easeInOutCubic(float t);
  static float shortestAngleLerp(float from, float to, float t);
  void drawFace();

  lgfx::LGFX_Device& _display;
  lgfx::LGFX_Sprite _backbuffer;

  float _startAngle = 0.0f;
  float _targetAngle = 0.0f;
  float _currentAngle = 0.0f;

  uint32_t _moveElapsedUs = 0;
  uint32_t _moveDurationUs = 300'000;  // ~300ms ease per move, tune visually
};
