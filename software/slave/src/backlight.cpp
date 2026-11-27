#include "backlight.h"   // own interface

#include <Arduino.h>     // analogWrite
#include "board.h"       // PIN_BACKLIGHT, BOOT_BRIGHTNESS

namespace backlight {

void begin() {
    analogWriteFreq(1000);      // 1 kHz, as tested in phase 5.1 (H5-H7)
    analogWriteRange(255);      // duty 0..255, the same range as protocol::DisplayData::brightness
    set(BOOT_BRIGHTNESS);       // 70 % until the first CMD_DISPLAY
}

// No gamma here: the master sends the final duty (see the master README).
void set(uint8_t duty) {
    analogWrite(PIN_BACKLIGHT, duty);
}

}  // namespace backlight
