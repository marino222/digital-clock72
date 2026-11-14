#pragma once

#include <cstdint>

// Display wiring, identical on the breadboard and the node PCB v1.0
constexpr int PIN_SCLK = 18; // SCL
constexpr int PIN_MOSI = 19;  // SDA
constexpr int PIN_MISO = -1;  // not wired, GC9A01 used write-only
constexpr int PIN_CS = 20;
constexpr int PIN_DC = 17;
constexpr int PIN_RST = 16;
// Backlight: on the node PCB, GPIO21 (BL_PWM) drives the base of Q1 (S8050)
// through R13 (2.2 kOhm), and Q1 switches LEDK to ground. R14 (10 kOhm) pulls
// the base low, so the backlight stays off until the firmware drives the pin.
// GPIO21 is PWM slice 2, channel B.
constexpr int PIN_BL = 21;


// Starting point for the GC9A01 SPI write clock. No datasheet-guaranteed
// max exists for this exact breakout + breadboard wiring, so raise this
// (e.g. 60/80 MHz) and re-measure if the hand-animation benchmark falls
// short of 60 FPS. SPI bus clock speed on pico is max. 62.5 MHz. But 40 MHz is regared as the sweet spot.
constexpr uint32_t SPI_WRITE_HZ = 70'000'000;

