#pragma once

// Decides what is on screen (core 1). The views draw it.
//
// Owns the display. status_view, clock_face and widget_view get it through
// their begin(). Priority, highest first:
//
//   1. Version error    "Protocol vX, expected vY"
//   2. No address       "No address"
//   3. Diagnostic mode  "Row r" / "Pos p" / "Slot s"
//   4. Mode             clock or widget, whichever init came last
//   5. Nothing yet      "..."

namespace renderer {

// Starts the display. Call after core 0 has set the ready flag.
void begin();

// Takes the mailbox and draws what changed, as in phase 3.2:
// text screens only when they change, the clock on every new angle.
// Pauses 200 µs if nothing changed. Returns after one pass.
void draw();

}  // namespace renderer
