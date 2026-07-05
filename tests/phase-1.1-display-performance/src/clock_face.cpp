#include "clock_face.h"


#include <algorithm>
#include <cmath>

namespace {
constexpr int kSize = 240;
constexpr int kCenter = kSize / 2;
constexpr int kFaceRadius = 118;
constexpr int kTickInnerRadius = 104;
constexpr int kHandLength = 100;
constexpr int kHandHalfWidth = 4;

constexpr uint16_t kColorBackground = TFT_BLACK;
constexpr uint16_t kColorFace = TFT_DARKGREY;
constexpr uint16_t kColorHand = TFT_RED;
}  // namespace

ClockFace::ClockFace(lgfx::LGFX_Device& display)
    : _display(display), _backbuffer(&display) {}

void ClockFace::begin() {
  _backbuffer.setColorDepth(16);
  _backbuffer.createSprite(kSize, kSize);
}

void ClockFace::setTargetAngle(float degrees) {
  _startAngle = _currentAngle;
  _targetAngle = degrees;
  _moveElapsedUs = 0;
}

float ClockFace::easeInOutCubic(float t) {
  return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
}

float ClockFace::shortestAngleLerp(float from, float to, float t) {
  float delta = std::fmod(to - from + 540.0f, 360.0f) - 180.0f;
  float result = from + delta * t;
  return std::fmod(result + 360.0f, 360.0f);
}

void ClockFace::update(uint32_t dtMicros) {
  if (_moveElapsedUs >= _moveDurationUs) {
    _currentAngle = _targetAngle;
    return;
  }
  _moveElapsedUs += dtMicros;
  float t = std::min(1.0f, static_cast<float>(_moveElapsedUs) / static_cast<float>(_moveDurationUs));
  _currentAngle = shortestAngleLerp(_startAngle, _targetAngle, easeInOutCubic(t));
}

void ClockFace::drawFace() {
  _backbuffer.fillScreen(kColorBackground);
  _backbuffer.drawCircle(kCenter, kCenter, kFaceRadius, kColorFace);
  for (int i = 0; i < 12; ++i) {
    float angle = i * 30.0f * DEG_TO_RAD;
    int x0 = kCenter + static_cast<int>(kTickInnerRadius * sinf(angle));
    int y0 = kCenter - static_cast<int>(kTickInnerRadius * cosf(angle));
    int x1 = kCenter + static_cast<int>(kFaceRadius * sinf(angle));
    int y1 = kCenter - static_cast<int>(kFaceRadius * cosf(angle));
    _backbuffer.drawLine(x0, y0, x1, y1, kColorFace);
  }
}

void ClockFace::render() {
  drawFace();

  float angleRad = _currentAngle * DEG_TO_RAD;
  float dx = sinf(angleRad);
  float dy = -cosf(angleRad);
  // Perpendicular direction, used to give the hand a triangular width.
  float px = -dy;
  float py = dx;

  int tipX = kCenter + static_cast<int>(kHandLength * dx);
  int tipY = kCenter + static_cast<int>(kHandLength * dy);
  int baseX0 = kCenter + static_cast<int>(kHandHalfWidth * px);
  int baseY0 = kCenter + static_cast<int>(kHandHalfWidth * py);
  int baseX1 = kCenter - static_cast<int>(kHandHalfWidth * px);
  int baseY1 = kCenter - static_cast<int>(kHandHalfWidth * py);

  _backbuffer.fillTriangle(tipX, tipY, baseX0, baseY0, baseX1, baseY1, kColorHand);
  _backbuffer.fillCircle(kCenter, kCenter, kHandHalfWidth, kColorHand);

  _backbuffer.pushSprite(0, 0);
}
