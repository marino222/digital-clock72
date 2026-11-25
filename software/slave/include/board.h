#pragma once

// Pins and constants for the node PCB.
// GPIO numbers are from PCB v1. Names follow the slave README (v2 names).

// ---- ADDR chain ----
constexpr int PIN_ADDR_IN      = 9;   // J3
constexpr int PIN_ADDR_OUT     = 10;  // J2
constexpr int PIN_ADDR_ROW_OUT = 11;  // J3. Named SPARE_IN on PCB v1
constexpr int PIN_SPARE        = 8;   // J2. Named SPARE_OUT on PCB v1. Always an input

constexpr long ADDR_BAUD = 9600;

// ---- RS485 ----
constexpr int PIN_RS485_TX = 12;
constexpr int PIN_RS485_RX = 13;
constexpr int PIN_RS485_DE = 14;  // high = transmit. The slave keeps it low
constexpr int PIN_RS485_RE = 15;  // active low. The slave keeps it low (receive)

constexpr long RS485_BAUD = 1'000'000;

// ---- Display (GC9A01, SPI, write-only) ----
constexpr int PIN_DISP_RST  = 16;
constexpr int PIN_DISP_DC   = 17;
constexpr int PIN_DISP_SCK  = 18;
constexpr int PIN_DISP_MOSI = 19;
constexpr int PIN_DISP_CS   = 20;

// ---- Backlight ----
constexpr int PIN_BACKLIGHT = 21;  // drives Q1, PWM for brightness
