#pragma once
#include <LovyanGFX.hpp>
#include <cmath>
#include <algorithm>

#ifndef DEG_TO_RAD
#define DEG_TO_RAD (3.14159265359f / 180.0f)
#endif

// ---------------------------------------------------------------------------
// ClockFace (master-sprite / pushRotateZoom variant)
//
// Instead of rasterising an anti-aliased wedge line every frame (soft-float,
// no FPU on RP2040 -> ~20 ms), each hand is rasterised ONCE at startup into a
// small "master" sprite. Every frame the master is rotate-blitted onto the
// full-frame canvas with pushRotateZoom(). That turns per-pixel soft-float AA
// into an integer inverse-affine + source lookup, which is memory-bound rather
// than math-bound.
//
// Erase model (Phase 1.1): background is solid black, so old hand positions are
// cleared with fillRect(). A setBackground() hook is provided for when a static
// face (ticks / bezel) is added later -- see the caveat there.
//
// Display push: per-hand dirty rectangles (old-box UNION new-box), so the empty
// quadrant between the hands is never shipped over SPI.
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

  // Optional: attach a static background sprite (face graphics). Must be the
  // same size as the display and called BEFORE begin(). When set, erase
  // restores from it instead of solid black.
  // CAVEAT: the master hands are chroma-keyed on black, so their anti-aliased
  // edges fade to black. Against a non-black face this leaves a faint dark
  // fringe. For a real face, switch blitHand() to pushRotateZoomWithAA (which
  // blends edges against the destination) -- see blitHand().
  void setBackground(LGFX_Sprite* bg) { _bg = bg; }

  void begin() {
    _cx = _gfx.width()  / 2;
    _cy = _gfx.height() / 2;

    _hand1Length = int16_t(_radius * 0.6f);
    _hand2Length = int16_t(_radius * 0.9f);

    _frame.setColorDepth(16);
    _frame.createSprite(_gfx.width(), _gfx.height());
    if (_bg) _bg->pushSprite(&_frame, 0, 0);
    else     _frame.fillScreen(TFT_BLACK);

    createHand(_h1, _hand1Length, _hand1Thickness, _hand1Color);
    createHand(_h2, _hand2Length, _hand2Thickness, _hand2Color);
  }

  void prepareFrame(float angle1Deg, float angle2Deg) {
    // Output AABBs of the hands at their NEW angles
    Bounds new1 = boundsFor(_h1, angle1Deg);
    Bounds new2 = boundsFor(_h2, angle2Deg);

    if (_hasPrevFrame) {
      // Erase both OLD positions first, THEN draw both NEW hands on top.
      // (Erasing both before drawing keeps overlaps correct.)
      Bounds old1 = boundsFor(_h1, _prevAngle1);
      Bounds old2 = boundsFor(_h2, _prevAngle2);
      restoreRegion(old1);
      restoreRegion(old2);

      // Per-hand display region = where it was UNION where it now is.
      _push1 = new1; _push1.unite(old1);
      _push2 = new2; _push2.unite(old2);
    } else {
      _push1 = new1;
      _push2 = new2;
    }

    // Composite the new hands (integer rotate-blit, chroma-keyed on black)
    blitHand(_h1, angle1Deg);
    blitHand(_h2, angle2Deg);

    _prevAngle1 = angle1Deg;
    _prevAngle2 = angle2Deg;
  }

  void pushFrame() {
    if (!_hasPrevFrame) {
      // First frame: push everything (display contents unknown at init)
      _gfx.startWrite();
      _frame.pushSprite(&_gfx, 0, 0);
      _gfx.endWrite();
      _hasPrevFrame = true;
      _lastPayloadBytes = uint32_t(_gfx.width()) * _gfx.height() * 2;
      return;
    }

    _push1.clampTo(_gfx.width(), _gfx.height());
    _push2.clampTo(_gfx.width(), _gfx.height());

    _gfx.startWrite();                 // single SPI transaction for both windows
    pushRegion(_push1);
    pushRegion(_push2);
    _gfx.endWrite();

    // Note: overlapping pixels are counted (and pushed) twice. Harmless, and it
    // keeps the payload figure honest about bytes actually sent over SPI.
    _lastPayloadBytes = uint32_t(_push1.width() * _push1.height()
                               + _push2.width() * _push2.height()) * 2;
  }

  uint32_t getLastPayloadSize() const { return _lastPayloadBytes; }

private:
  // -------------------------------------------------------------------------
  struct Bounds {
    int16_t minX = INT16_MAX, minY = INT16_MAX;
    int16_t maxX = INT16_MIN, maxY = INT16_MIN;

    bool valid() const { return maxX >= minX && maxY >= minY; }

    // Expand outward: floor for the min side, ceil for the max side, so a
    // fractional rotated corner is never clipped.
    void expand(float x, float y) {
      minX = std::min<int16_t>(minX, int16_t(floorf(x)));
      minY = std::min<int16_t>(minY, int16_t(floorf(y)));
      maxX = std::max<int16_t>(maxX, int16_t(ceilf(x)));
      maxY = std::max<int16_t>(maxY, int16_t(ceilf(y)));
    }

    void unite(const Bounds& o) {
      if (!o.valid()) return;
      minX = std::min(minX, o.minX);
      minY = std::min(minY, o.minY);
      maxX = std::max(maxX, o.maxX);
      maxY = std::max(maxY, o.maxY);
    }

    void pad(int16_t p) {
      if (!valid()) return;
      minX -= p; minY -= p; maxX += p; maxY += p;
    }

    void clampTo(int16_t w, int16_t h) {
      minX = std::max<int16_t>(minX, 0);
      minY = std::max<int16_t>(minY, 0);
      maxX = std::min<int16_t>(maxX, int16_t(w - 1));
      maxY = std::min<int16_t>(maxY, int16_t(h - 1));
    }

    int16_t width()  const { return valid() ? int16_t(maxX - minX + 1) : 0; }
    int16_t height() const { return valid() ? int16_t(maxY - minY + 1) : 0; }
  };

  // A pre-rasterised hand plus the geometry needed to compute its footprint.
  struct Hand {
    LGFX_Sprite spr;
    int16_t w = 0, h = 0;      // master sprite size
    float   px = 0, py = 0;    // pivot within the master (base of the hand)
  };

  // -------------------------------------------------------------------------
  // Rasterise one hand ONCE, pointing "up" (toward -y), pivot at the base cap.
  void createHand(Hand& hd, int16_t length, uint8_t thickness, uint16_t color) {
    const int16_t m = 2;                         // AA safety margin (px)
    hd.w = int16_t(thickness + 2 * m);
    hd.h = int16_t(length + thickness + 2 * m);  // + tip cap + base cap + margin
    hd.px = hd.w / 2.0f;
    hd.py = hd.h - thickness / 2.0f - m;         // base cap centre

    hd.spr.setColorDepth(16);
    hd.spr.createSprite(hd.w, hd.h);
    hd.spr.fillScreen(kChroma);                  // chroma key == TFT_BLACK
    // Capsule from base (pivot) up to the tip. AA is baked in here, once.
    hd.spr.drawWedgeLine(hd.px, hd.py,
                         hd.px, hd.py - length,
                         thickness / 2.0f, thickness / 2.0f, color);
    hd.spr.setPivot(hd.px, hd.py);
  }

  // Exact AABB of the master's four corners after rotation about the pivot,
  // placed at the clock centre. Matches the transform pushRotateZoom applies
  // for a positive (clockwise, screen-space) angle.
  Bounds boundsFor(const Hand& hd, float angleDeg) const {
    float rad = angleDeg * DEG_TO_RAD;
    float c = cosf(rad), s = sinf(rad);

    const float ox[4] = { -hd.px, hd.w - hd.px, hd.w - hd.px, -hd.px };
    const float oy[4] = { -hd.py, -hd.py, hd.h - hd.py, hd.h - hd.py };

    Bounds b;
    for (int i = 0; i < 4; ++i) {
      float dx = ox[i] * c - oy[i] * s + _cx;
      float dy = ox[i] * s + oy[i] * c + _cy;
      b.expand(dx, dy);
    }
    b.pad(1);   // 1 px slack against rounding in the rotate resampler
    return b;
  }

  // Rotate-blit a hand onto the frame canvas, keying out the black filler.
  void blitHand(Hand& hd, float angleDeg) {
    // If rotation runs the wrong way on your LGFX build, negate angleDeg.
    hd.spr.pushRotateZoom(&_frame, _cx, _cy, angleDeg, 1.0f, 1.0f, kChroma);
    // Quality alternative (softer edges against a non-black face, ~2-3x cost):
    // hd.spr.pushRotateZoomWithAA(&_frame, _cx, _cy, angleDeg, 1.0f, 1.0f, kChroma);
  }

  // Erase a region back to background (solid black, or from _bg if attached).
  void restoreRegion(Bounds b) {
    b.clampTo(_gfx.width(), _gfx.height());
    if (!b.valid()) return;
    if (_bg) {
      _frame.setClipRect(b.minX, b.minY, b.width(), b.height());
      _bg->pushSprite(&_frame, 0, 0);
      _frame.clearClipRect();
    } else {
      _frame.fillRect(b.minX, b.minY, b.width(), b.height(), TFT_BLACK);
    }
  }

  // Push one clipped rectangle of the frame canvas to the display.
  void pushRegion(const Bounds& b) {
    if (!b.valid()) return;
    _gfx.setClipRect(b.minX, b.minY, b.width(), b.height());
    _frame.pushSprite(&_gfx, 0, 0);
    _gfx.clearClipRect();
  }

  // -------------------------------------------------------------------------
  static constexpr uint16_t kChroma = 0x0000;  // TFT_BLACK, used as transparent key

  LGFX_Device& _gfx;
  LGFX_Sprite  _frame;
  LGFX_Sprite* _bg = nullptr;

  Hand _h1, _h2;

  Bounds _push1, _push2;   // display regions carried from prepare -> push

  int16_t _radius, _cx = 0, _cy = 0;
  int16_t _hand1Length = 0, _hand2Length = 0;
  uint16_t _hand1Color, _hand2Color;
  uint8_t _hand1Thickness, _hand2Thickness;

  bool  _hasPrevFrame = false;
  float _prevAngle1 = 0.0f, _prevAngle2 = 0.0f;
  uint32_t _lastPayloadBytes = 0;
};