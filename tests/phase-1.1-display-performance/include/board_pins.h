#pragma once

#include <cstdint>

// Definition of breadboard wiring
constexpr int PIN_SCLK = 18; // SCL
constexpr int PIN_MOSI = 19;  // SDA
constexpr int PIN_MISO = -1;  // not wired, GC9A01 used write-only
constexpr int PIN_CS = 20;
constexpr int PIN_DC = 17;
constexpr int PIN_RST = 16;
constexpr int PIN_BL = -1;  // backlight tied to a fixed rail on this breakout


// Starting point for the GC9A01 SPI write clock. No datasheet-guaranteed
// max exists for this exact breakout + breadboard wiring, so raise this
// (e.g. 60/80 MHz) and re-measure if the hand-animation benchmark falls
// short of 60 FPS. SPI bus clock speed on pico is max. 62.5 MHz. But 40 MHz is regared as the sweet spot.
constexpr uint32_t SPI_WRITE_HZ = 70'000'000;

