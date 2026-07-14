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
  }

  void update(float angle1Deg, float angle2Deg) {
    Bounds b;
    if (_hasPrevFrame) {
      expandForHand(b, _prevAngle1, _hand1Length, _hand1Thickness);
      expandForHand(b, _prevAngle2, _hand2Length, _hand2Thickness);
    }
    expandForHand(b, angle1Deg, _hand1Length, _hand1Thickness);
    expandForHand(b, angle2Deg, _hand2Length, _hand2Thickness);

    _frame.fillScreen(TFT_BLACK);
    drawHand(angle1Deg, _hand1Length, _hand1Thickness, _hand1Color);
    drawHand(angle2Deg, _hand2Length, _hand2Thickness, _hand2Color);

    if (_hasPrevFrame) {
      b.clampTo(0, 0, _gfx.width(), _gfx.height());
      _gfx.startWrite();
      _gfx.setClipRect(b.minX, b.minY, b.width(), b.height());
      _frame.pushSprite(&_gfx, 0, 0);
      _gfx.clearClipRect();
      _gfx.endWrite();
    } else {
      _frame.pushSprite(&_gfx, 0, 0);
      _hasPrevFrame = true;
    }

    _prevAngle1 = angle1Deg;
    _prevAngle2 = angle2Deg;
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

    int16_t width()  const { return maxX - minX + 1; } // +1: bounds are inclusive
    int16_t height() const { return maxY - minY + 1; }
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
  int16_t _radius, _cx = 0, _cy = 0;
  int16_t _hand1Length = 0, _hand2Length = 0;
  uint16_t _hand1Color, _hand2Color;
  uint8_t _hand1Thickness, _hand2Thickness;

  bool _hasPrevFrame = false;
  float _prevAngle1 = 0.0f, _prevAngle2 = 0.0f;
};