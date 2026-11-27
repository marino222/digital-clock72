#pragma once
#include <stdint.h>
#include "mailbox.h"   // mailbox::NodeAddress

class LGFX;            // the display class, from LGFX_Config.hpp

// Draws the text screens (core 1). renderer decides which one, this module only draws.
// Each call fills the whole screen.

namespace status_view {

// Remembers the display. renderer owns it and passes it in once.
void begin(LGFX& display);

void drawVersionError(uint8_t received);                  // red: "Protocol vX" / "expected vY"
void drawNoAddress();                                     // "No address" in red
void drawAddress(const mailbox::NodeAddress& address);    // "Row r" / "Pos p" / "Slot s"
void drawWaiting();                                       // "..."

}  // namespace status_view
