#pragma once
#include <LovyanGFX.hpp>
#include <cmath>
#include <algorithm>

#ifndef DEG_TO_RAD
#define DEG_TO_RAD (3.14159265359f / 180.0f)
#endif

class ClockFace {
public:
  ClockFace(LGFX_Device& gfx, int16_t radius,
            uint16_t hand1Color, uint16_t hand2Color,
            uint8_t hand1Thickness, uint8_t hand2Thickness)
    : _gfx(gfx), _radius(radius),
      _hand1Color(hand1Color), _hand2Color(hand2Color),
      _hand1Thickness(hand1Thickness), _hand2Thickness(hand2Thickness)
  {}

  void begin() {
    _cx = _gfx.width() / 2;
    _cy = _gfx.height() / 2;
    _hand1Length = _radius * 0.6f;
    _hand2Length = _radius * 0.9f;

    _frame.setColorDepth(16);
    _frame.createSprite(_gfx.width(), _gfx.height());
    _frame.fillScreen(TFT_BLACK); // Only clear the screen ONCE at startup
  }

  void prepareFrame(float angle1Deg, float angle2Deg) {
    // Reset bounding box for this frame
    _currentBounds = Bounds();

    if (_hasPrevFrame) {
      // 1. Expand bounding box to include where the OLD hands were
      expandForHand(_currentBounds, _prevAngle1, _hand1Length, _hand1Thickness);
      expandForHand(_currentBounds, _prevAngle2, _hand2Length, _hand2Thickness);
      
      // 2. TRUE DIRTY RECTANGLE: Erase the old hands by drawing them in BLACK
      drawHand(_prevAngle1, _hand1Length, _hand1Thickness, TFT_BLACK);
      drawHand(_prevAngle2, _hand2Length, _hand2Thickness, TFT_BLACK);
    }

    // 3. Expand bounding box to include where the NEW hands will be
    expandForHand(_currentBounds, angle1Deg, _hand1Length, _hand1Thickness);
    expandForHand(_currentBounds, angle2Deg, _hand2Length, _hand2Thickness);

    // 4. Draw the new hands in color
    drawHand(angle1Deg, _hand1Length, _hand1Thickness, _hand1Color);
    drawHand(angle2Deg, _hand2Length, _hand2Thickness, _hand2Color);

    // Save angles for the next frame
    _prevAngle1 = angle1Deg;
    _prevAngle2 = angle2Deg;
  }

  void pushFrame() {
    if (_hasPrevFrame) {
      _currentBounds.clampTo(0, 0, _gfx.width(), _gfx.height());
      
      _gfx.startWrite();
      // Only push the small rectangle that actually changed
      _gfx.setClipRect(_currentBounds.minX, _currentBounds.minY, _currentBounds.width(), _currentBounds.height());
      _frame.pushSprite(&_gfx, 0, 0);
      _gfx.clearClipRect();
      _gfx.endWrite();

      // Calculate the size of the payload we just pushed
      _lastPayloadBytes = _currentBounds.width() * _currentBounds.height() * 2;
    } else {
      // First frame pushes everything
      _gfx.startWrite();
      _frame.pushSprite(&_gfx, 0, 0);
      _gfx.endWrite();
      _hasPrevFrame = true;
      _lastPayloadBytes = _gfx.width() * _gfx.height() * 2;
    }
  }

  // Returns the size of the clipped rectangle sent to the display
  uint32_t getLastPayloadSize() const {
    return _lastPayloadBytes;
  }

private:
  struct Bounds {
    int16_t minX = INT16_MAX, minY = INT16_MAX;
    int16_t maxX = INT16_MIN, maxY = INT16_MIN;

    void include(int16_t x, int16_t y, int16_t pad) {
      minX = std::min(minX, int16_t(x - pad));
      minY = std::min(minY, int16_t(y - pad));
      maxX = std::max(maxX, int16_t(x + pad));
      maxY = std::max(maxY, int16_t(y + pad));
    }

    void clampTo(int16_t loX, int16_t loY, int16_t hiX, int16_t hiY) {
      minX = std::max(minX, loX);
      minY = std::max(minY, loY);
      maxX = std::min(maxX, int16_t(hiX - 1));
      maxY = std::min(maxY, int16_t(hiY - 1));
    }

    int16_t width()  const { return std::max(0, maxX - minX + 1); }
    int16_t height() const { return std::max(0, maxY - minY + 1); }
  };

  void handTip(float angleDeg, int16_t length, int16_t& x, int16_t& y) {
    float rad = angleDeg * DEG_TO_RAD;
    x = _cx + int16_t(lroundf(sinf(rad) * length));
    y = _cy - int16_t(lroundf(cosf(rad) * length));
  }

  void drawHand(float angleDeg, int16_t length, uint8_t thickness, uint16_t color) {
    int16_t x, y;
    handTip(angleDeg, length, x, y);
    _frame.drawWedgeLine(_cx, _cy, x, y, thickness / 2.0f, thickness / 2.0f, color);
  }

  void expandForHand(Bounds& b, float angleDeg, int16_t length, uint8_t thickness) {
    int16_t x, y;
    handTip(angleDeg, length, x, y);
    int16_t pad = (thickness + 1) / 2 + 3; // round half-thickness up + AA safety margin
    b.include(_cx, _cy, pad); // base cap
    b.include(x, y, pad);     // tip cap
  }

  LGFX_Device& _gfx;
  LGFX_Sprite _frame;
  Bounds _currentBounds; // Store bounds between prepare() and push()

  int16_t _radius, _cx = 0, _cy = 0;
  int16_t _hand1Length = 0, _hand2Length = 0;
  uint16_t _hand1Color, _hand2Color;
  uint8_t _hand1Thickness, _hand2Thickness;

  bool _hasPrevFrame = false;
  float _prevAngle1 = 0.0f, _prevAngle2 = 0.0f;
  uint32_t _lastPayloadBytes = 0;
};