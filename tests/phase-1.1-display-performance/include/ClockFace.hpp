#pragma once
#include <LovyanGFX.hpp>
#include <cmath>

#ifndef DEG_TO_RAD
#define DEG_TO_RAD (3.14159265359f / 180.0f)
#endif

class ClockFace {
public:
  ClockFace(LGFX_Device& gfx, int16_t radius,
            uint16_t hourColor, uint16_t minuteColor, uint16_t secondColor,
            uint8_t hourThickness, uint8_t minuteThickness, uint8_t secondThickness)
    : _gfx(gfx), _radius(radius),
      _hourColor(hourColor), _minuteColor(minuteColor), _secondColor(secondColor),
      _hourThickness(hourThickness), _minuteThickness(minuteThickness), _secondThickness(secondThickness)
  {}

  void begin() {
    _cx = _gfx.width() / 2;
    _cy = _gfx.height() / 2;
    _frame.setColorDepth(16);
    _frame.createSprite(_gfx.width(), _gfx.height());
  }

  void update(float hours, float minutes, float seconds) {
    _frame.fillScreen(TFT_BLACK);
    _frame.drawCircle(_cx, _cy, _radius, TFT_DARKGREY);
    for (int i = 0; i < 12; i++) {
      drawTick(i * 30.0f);
    }
    drawHand(fmodf(hours, 12.0f) * 30.0f, _radius * 0.5f, _hourThickness, _hourColor);
    drawHand(fmodf(minutes, 60.0f) * 6.0f, _radius * 0.75f, _minuteThickness, _minuteColor);
    drawHand(fmodf(seconds, 60.0f) * 6.0f, _radius * 0.9f, _secondThickness, _secondColor);
    _frame.pushSprite(&_gfx, 0, 0);
  }

private:
  void drawTick(float angleDeg) {
    float rad = angleDeg * DEG_TO_RAD;
    int16_t x1 = _cx + sinf(rad) * (_radius - 10);
    int16_t y1 = _cy - cosf(rad) * (_radius - 10);
    int16_t x2 = _cx + sinf(rad) * _radius;
    int16_t y2 = _cy - cosf(rad) * _radius;
    _frame.drawLine(x1, y1, x2, y2, TFT_DARKGREY);
  }

  void drawHand(float angleDeg, int16_t length, uint8_t thickness, uint16_t color) {
    float rad = angleDeg * DEG_TO_RAD;
    int16_t x = _cx + sinf(rad) * length;
    int16_t y = _cy - cosf(rad) * length;
    _frame.drawWedgeLine(_cx, _cy, x, y, thickness / 2.0f, thickness / 2.0f, color);
  }

  LGFX_Device& _gfx;
  LGFX_Sprite _frame;
  int16_t _radius, _cx = 0, _cy = 0;
  uint16_t _hourColor, _minuteColor, _secondColor;
  uint8_t _hourThickness, _minuteThickness, _secondThickness;
};
