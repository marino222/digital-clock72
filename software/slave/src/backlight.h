#pragma once
#include <stdint.h>

// PWM on PIN_BACKLIGHT (core 1).

namespace backlight {

// Starts the PWM at BOOT_BRIGHTNESS.
void begin();

// 0 = off, 255 = full. Called by renderer with protocol::DisplayData::brightness.
void set(uint8_t duty);

}  // namespace backlight
