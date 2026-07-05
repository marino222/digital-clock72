#pragma once

#include <cstdint>

// Breadboard wiring for Phase 1.1, see tests/README.md.
// This is the only file that should need to change for a different
// board revision (e.g. the final node PCB pinout).
namespace board {

constexpr int PIN_SCLK = 19;
constexpr int PIN_MOSI = 18;
constexpr int PIN_MISO = -1;  // not wired, GC9A01 used write-only
constexpr int PIN_CS = 20;
constexpr int PIN_DC = 17;
constexpr int PIN_RST = 16;
constexpr int PIN_BL = -1;  // backlight tied to a fixed rail on this breakout

// Unused GPIO, optionally toggled once per frame for an oscilloscope /
// logic-analyzer cross-check of the FPS measurement. See fps_counter.h.
constexpr int PIN_SCOPE_DEBUG = 21;

// Starting point for the GC9A01 SPI write clock. No datasheet-guaranteed
// max exists for this exact breakout + breadboard wiring, so raise this
// (e.g. 60/80 MHz) and re-measure if the hand-animation benchmark falls
// short of 60 FPS.
constexpr uint32_t SPI_WRITE_HZ = 40'000'000;

}  // namespace board
