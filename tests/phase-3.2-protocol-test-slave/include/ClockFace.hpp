#pragma once
#include <LovyanGFX.hpp>
#include <cmath>
#include <algorithm>

#ifndef DEG_TO_RAD
#define DEG_TO_RAD (3.14159265359f / 180.0f)
#endif

// ---------------------------------------------------------------------------
// ClockFace
//
// Draws two rotating clock hands and pushes only the pixels that changed to
// the display, so a 240x240 display can keep up with 60 FPS over SPI.
//
// Why not just redraw everything, every frame?
//  - drawWedgeLine() (an anti-aliased line) is slow on the RP2040: it uses
//    floating point math and the chip has no hardware FPU. Drawing both
//    hands with it every frame took 20+ ms, which alone blows the ~16.6 ms
//    frame budget for 60 FPS.
//  - Sending the whole 240x240 frame over SPI every frame is also too slow.
//
// How this class avoids both costs:
//  1. Each hand is drawn ONCE at startup with drawWedgeLine(), into its own
//     small sprite (see createHand()). That sprite is reused every frame.
//  2. Every frame, that small sprite is rotated into place on the full-size
//     frame buffer (see blitHand()). Rotating an already-drawn image is far
//     cheaper than re-drawing an anti-aliased line from scratch.
//  3. Only the small rectangle of the screen a hand touches -- its old spot
//     plus its new spot -- is cleared and re-sent over SPI, not the whole
//     screen (see prepareFrame() / pushFrame()).
//
// ---------------------------------------------------------------------------
class ClockFace {
public:
  ClockFace(LGFX_Device& gfx, int16_t radius,
            uint16_t hand1Color, uint16_t hand2Color,
            uint8_t hand1Thickness, uint8_t hand2Thickness)
    : _gfx(gfx), _radius(radius),
      _hand1Color(hand1Color), _hand2Color(hand2Color),
      _hand1Thickness(hand1Thickness), _hand2Thickness(hand2Thickness)
  {}

  // Applies a CMD_CLOCK_INIT payload. Must be called BEFORE begin(), since
  // begin() is what bakes these values into the pre-drawn hand sprites.
  //
  // Note there is no background colour here: the rotation trick in blitHand()
  // uses black as its transparent colour (kChroma), so the face background has
  // to stay black. ClockInitData carries a bgColor for later, once the chroma
  // key and the background are decoupled.
  void configure(uint16_t hand1Color, uint16_t hand2Color,
                 uint8_t hand1Thickness, uint8_t hand2Thickness,
                 uint8_t hand1LengthPct, uint8_t hand2LengthPct) {
    _hand1Color     = hand1Color;
    _hand2Color     = hand2Color;
    _hand1Thickness = hand1Thickness;
    _hand2Thickness = hand2Thickness;
    _hand1LengthPct = hand1LengthPct;
    _hand2LengthPct = hand2LengthPct;
  }

  void begin() {
    _cx = _gfx.width()  / 2;
    _cy = _gfx.height() / 2;

    _hand1Length = int16_t(_radius * _hand1LengthPct / 100);
    _hand2Length = int16_t(_radius * _hand2LengthPct / 100);

    _frame.setColorDepth(16);
    _frame.createSprite(_gfx.width(), _gfx.height());
    _frame.fillScreen(TFT_BLACK);

    createHand(_h1, _hand1Length, _hand1Thickness, _hand1Color);
    createHand(_h2, _hand2Length, _hand2Thickness, _hand2Color);

    // begin() runs again every time a fresh CMD_CLOCK_INIT reconfigures the
    // face. The frame buffer above is blank again, but the DISPLAY still shows
    // the hands from the old configuration -- so the next push has to be a
    // full-screen one, or those old hands stay burned in everywhere the new
    // dirty rectangles don't happen to cover.
    invalidate();
  }

  // Forces the next pushFrame() to send the whole screen instead of just the
  // hands' dirty rectangles. Call this whenever something drew over the display
  // behind ClockFace's back -- a widget frame taking the screen over, or a
  // re-init -- otherwise pushFrame() only repaints where it thinks a hand was.
  void invalidate() {
    _hasPrevFrame = false;
    _lastBounds1 = Bounds();
    _lastBounds2 = Bounds();
  }

  // Advances both hands to the given angles and updates the off-screen
  // frame buffer (_frame) to match. Does NOT touch the display yet -- that
  // happens in pushFrame(). Also records, in _push1/_push2, which parts of
  // the screen need to be sent this frame.
  //
  // Angles are in TENTHS OF A DEGREE (0..3599), clockwise from 12 o'clock.
  // This matches the angle fields in the RS485 packet, so a received angle
  // can be handed straight to this function with no conversion.
  //
  // Whole degrees weren't enough resolution: at the rates these test rigs
  // run at, a hand can advance less than 1 degree per frame, so a
  // whole-degree quantity steps unevenly (e.g. alternating +0/+1 degree
  // frame to frame) even though the underlying motion is constant speed.
  // Tenths of a degree keep that quantization error far enough below one
  // visible pixel that the stepping disappears.
  void prepareFrame(uint16_t angle1DeciDeg, uint16_t angle2DeciDeg) {
    // Where each hand will be after this update.
    Bounds new1 = boundsFor(_h1, angle1DeciDeg);
    Bounds new2 = boundsFor(_h2, angle2DeciDeg);

    if (_hasPrevFrame) {
      // Clear each hand from where it was last frame. _lastBounds1/2 were
      // saved the previous time this ran, so there's no need to redo the
      // rotation math to find them again.
      restoreRegion(_lastBounds1);
      restoreRegion(_lastBounds2);

      // The screen area to redraw for a hand is "where it used to be" plus
      // "where it is now" -- otherwise either its old trail or its new
      // position would be missed.
      _push1 = new1; _push1.unite(_lastBounds1);
      _push2 = new2; _push2.unite(_lastBounds2);
    } else {
      // Nothing on screen to erase yet, so just draw the new position.
      _push1 = new1;
      _push2 = new2;
    }

    // Draw both hands at their new angle on top of the (now erased) canvas.
    blitHand(_h1, angle1DeciDeg);
    blitHand(_h2, angle2DeciDeg);

    // Remember this frame's boxes so next frame knows what to erase.
    _lastBounds1 = new1;
    _lastBounds2 = new2;
  }

  // Sends the parts of the frame buffer that changed to the real display.
  void pushFrame() {
    if (!_hasPrevFrame) {
      // First frame ever: the display's current contents are unknown, so
      // the whole screen has to be sent once. After this, only dirty
      // rectangles are pushed.
      _gfx.startWrite();
      _frame.pushSprite(&_gfx, 0, 0);
      _gfx.endWrite();
      _hasPrevFrame = true;
      _lastPayloadBytes = uint32_t(_gfx.width()) * _gfx.height() * 2;
      return;
    }

    _push1.clampTo(_gfx.width(), _gfx.height());
    _push2.clampTo(_gfx.width(), _gfx.height());

    _gfx.startWrite();                 // one SPI transaction for both rectangles
    pushRegion(_push1);
    pushRegion(_push2);
    _gfx.endWrite();

    // If the two rectangles overlap, the shared pixels get sent twice. This
    // is harmless (both hands still draw correctly) and keeps this payload
    // number an honest count of bytes actually put on the wire.
    _lastPayloadBytes = uint32_t(_push1.width() * _push1.height()
                               + _push2.width() * _push2.height()) * 2;
  }

  uint32_t getLastPayloadSize() const { return _lastPayloadBytes; }

private:
  // A rectangle on the display, defined by its corners. Used to track which
  // area a hand occupies, so only that area needs to be erased/redrawn.
  struct Bounds {
    int16_t minX = INT16_MAX, minY = INT16_MAX;
    int16_t maxX = INT16_MIN, maxY = INT16_MIN;

    bool valid() const { return maxX >= minX && maxY >= minY; }

    // Grow the rectangle just enough to include point (x, y). Rounds
    // outward (floor/ceil) so a fractional corner is never cut off.
    void expand(float x, float y) {
      minX = std::min<int16_t>(minX, int16_t(floorf(x)));
      minY = std::min<int16_t>(minY, int16_t(floorf(y)));
      maxX = std::max<int16_t>(maxX, int16_t(ceilf(x)));
      maxY = std::max<int16_t>(maxY, int16_t(ceilf(y)));
    }

    // Grow this rectangle to also cover another one.
    void unite(const Bounds& o) {
      if (!o.valid()) return;
      minX = std::min(minX, o.minX);
      minY = std::min(minY, o.minY);
      maxX = std::max(maxX, o.maxX);
      maxY = std::max(maxY, o.maxY);
    }

    // Grow the rectangle by p pixels on every side (a small safety margin).
    void pad(int16_t p) {
      if (!valid()) return;
      minX -= p; minY -= p; maxX += p; maxY += p;
    }

    // Cut the rectangle down so it fits inside a w x h screen.
    void clampTo(int16_t w, int16_t h) {
      minX = std::max<int16_t>(minX, 0);
      minY = std::max<int16_t>(minY, 0);
      maxX = std::min<int16_t>(maxX, int16_t(w - 1));
      maxY = std::min<int16_t>(maxY, int16_t(h - 1));
    }

    int16_t width()  const { return valid() ? int16_t(maxX - minX + 1) : 0; }
    int16_t height() const { return valid() ? int16_t(maxY - minY + 1) : 0; }
  };

  // One clock hand, pre-drawn once into its own small sprite.
  struct Hand {
    LGFX_Sprite spr;
    int16_t w = 0, h = 0;      // size of the sprite
    float   px = 0, py = 0;    // pivot point within the sprite (where the hand attaches to the clock center)
  };

  // Draws one hand ONCE, pointing straight up, into its own small sprite.
  void createHand(Hand& hd, int16_t length, uint8_t thickness, uint16_t color) {
    const int16_t m = 2;                         // margin around the line, for AA (px)
    hd.w = int16_t(thickness + 2 * m);
    hd.h = int16_t(length + thickness + 2 * m);  // tip + base + margin
    hd.px = hd.w / 2.0f;
    hd.py = hd.h - thickness / 2.0f - m;         // pivot = center of the base

    hd.spr.setColorDepth(16);
    hd.spr.createSprite(hd.w, hd.h);
    hd.spr.fillScreen(kChroma);                  // fill with the "transparent" color
    // A rounded line (capsule) from the pivot up to the tip.
    hd.spr.drawWedgeLine(hd.px, hd.py,
                         hd.px, hd.py - length,
                         thickness / 2.0f, thickness / 2.0f, color);
    hd.spr.setPivot(hd.px, hd.py);
  }

  // Works out the rectangle of the screen a hand covers once rotated to
  // angleDeciDeg and placed at the clock's center.
  //
  // How: a hand sprite is a small rectangle with 4 corners. Rotating the
  // hand means rotating those 4 corners around the pivot point by the angle,
  // then placing them at the clock center. The smallest rectangle that
  // contains all 4 rotated corners is the hand's on-screen bounding box.
  // A 1px margin is added at the end, since the rotate function resamples
  // pixels and can touch a pixel just outside that exact box.
  Bounds boundsFor(const Hand& hd, uint16_t angleDeciDeg) const {
    float rad = (angleDeciDeg * kDegPerDeciDeg) * DEG_TO_RAD;
    float c = cosf(rad), s = sinf(rad);

    // The 4 corners of the hand sprite, measured from the pivot point.
    const float cornerX[4] = { -hd.px, hd.w - hd.px, hd.w - hd.px, -hd.px };
    const float cornerY[4] = { -hd.py, -hd.py, hd.h - hd.py, hd.h - hd.py };

    Bounds b;
    for (int i = 0; i < 4; ++i) {
      // Standard 2D rotation of (cornerX, cornerY) by angleDeg, then shift
      // so the pivot lands on the clock center (_cx, _cy).
      float x = cornerX[i] * c - cornerY[i] * s + _cx;
      float y = cornerX[i] * s + cornerY[i] * c + _cy;
      b.expand(x, y);
    }
    b.pad(1);
    return b;
  }

  // Rotates a hand's pre-drawn sprite onto the frame buffer at angleDeciDeg.
  void blitHand(Hand& hd, uint16_t angleDeciDeg) {
    // If the hands appear to spin the wrong way on your LGFX build, negate
    // the angle here.
    // LGFX takes the angle as a float degree value, so this is where the
    // conversion out of tenths-of-a-degree has to happen -- it can't be
    // pushed any further down.
    hd.spr.pushRotateZoomWithAA(&_frame, _cx, _cy, angleDeciDeg * kDegPerDeciDeg, 1.0f, 1.0f, kChroma);
  }

  // Restores one rectangle of the frame buffer back to plain black.
  void restoreRegion(Bounds b) {
    b.clampTo(_gfx.width(), _gfx.height());
    if (!b.valid()) return;
    _frame.fillRect(b.minX, b.minY, b.width(), b.height(), TFT_BLACK);
  }

  // Sends one rectangle of the frame buffer to the real display.
  void pushRegion(const Bounds& b) {
    if (!b.valid()) return;
    _gfx.setClipRect(b.minX, b.minY, b.width(), b.height());
    _frame.pushSprite(&_gfx, 0, 0);
    _gfx.clearClipRect();
  }

  // -------------------------------------------------------------------------
  static constexpr uint16_t kChroma = 0x0000;  // TFT_BLACK, treated as "transparent" when rotating a hand in
  static constexpr float kDegPerDeciDeg = 0.1f;  // converts an incoming angle from tenths of a degree to degrees

  LGFX_Device& _gfx;
  LGFX_Sprite  _frame;          // full-size off-screen buffer we draw into

  Hand _h1, _h2;

  Bounds _push1, _push2;        // this frame's screen regions to send, set in prepareFrame(), used in pushFrame()
  Bounds _lastBounds1, _lastBounds2; // each hand's box from the last frame, so next frame knows what to erase

  int16_t _radius, _cx = 0, _cy = 0;
  int16_t _hand1Length = 0, _hand2Length = 0;
  uint16_t _hand1Color, _hand2Color;
  uint8_t _hand1Thickness, _hand2Thickness;
  // Hand lengths as a percentage of the face radius. These were hard-coded at
  // 60/90 in phase 1.1; CMD_CLOCK_INIT can now override them via configure().
  uint8_t _hand1LengthPct = 60, _hand2LengthPct = 90;

  bool  _hasPrevFrame = false;
  uint32_t _lastPayloadBytes = 0;
};
