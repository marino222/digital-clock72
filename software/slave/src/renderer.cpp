#include "renderer.h"       // own interface

#include <Arduino.h>        // delayMicroseconds
#include "LGFX_Config.hpp"  // LGFX, the display class
#include "board.h"          // DEFAULT_ROTATION
#include "mailbox.h"        // mailbox::take(), mailbox::State
#include "backlight.h"      // backlight::set()
#include "status_view.h"    // status_view::draw...()

namespace renderer {

namespace {

// The five screens, in priority order (README, data flow).
enum Screen : uint8_t {
    SCREEN_VERSION,       // 1: "Protocol vX, expected vY"
    SCREEN_NO_ADDRESS,    // 2: "No address"
    SCREEN_ADDRESS,       // 3: row, pos, slot
    SCREEN_CONTENT,       // 4: clock or widget (next slice)
    SCREEN_WAITING,       // 5: "..."
    SCREEN_NONE           // nothing drawn yet, so the first draw() always draws
};

LGFX            display;                 // owned here. clock_face and widget_view get it later
mailbox::State  state;                   // our copy of the mailbox, filled by take()
Screen          shown = SCREEN_NONE;     // what is on the glass right now

// The priority table: the first condition that is true wins.
Screen pickScreen(const mailbox::State& s) {
    if (s.badVersion != 0)                            return SCREEN_VERSION;
    if (!s.address.valid)                             return SCREEN_NO_ADDRESS;
    if (s.diagMode == protocol::DIAG_SHOW_ADDRESS)    return SCREEN_ADDRESS;
    if (s.mode != mailbox::MODE_NONE)                 return SCREEN_CONTENT;
    return SCREEN_WAITING;
}

// Does the screen that is already shown need a redraw because its content changed?
bool contentChanged(Screen screen, uint16_t changed) {
    switch (screen) {
        case SCREEN_VERSION: return changed & mailbox::CHANGED_VERSION;   // a different wrong version
        case SCREEN_ADDRESS: return changed & mailbox::CHANGED_ADDRESS;   // a new address
        default:             return false;   // "No address" and "..." never change
    }
}

// Calls the view that draws `screen`. The views do the drawing, renderer only decides.
void drawScreen(Screen screen, const mailbox::State& s) {
    switch (screen) {
        case SCREEN_VERSION:    status_view::drawVersionError(s.badVersion); break;
        case SCREEN_NO_ADDRESS: status_view::drawNoAddress();                break;
        case SCREEN_ADDRESS:    status_view::drawAddress(s.address);         break;
        case SCREEN_WAITING:    status_view::drawWaiting();                  break;
        default:                display.fillScreen(TFT_BLACK);               break;   // SCREEN_CONTENT: clock_face / widget_view, next slice
    }
}

}  // namespace

void begin() {
    display.init();
    display.setRotation(DEFAULT_ROTATION);   // until the first CMD_DISPLAY
    display.fillScreen(TFT_BLACK);
    status_view::begin(display);
}

void draw() {
    if (!mailbox::take(state)) {
        delayMicroseconds(200);              // nothing new: let core 0 have the lock
        return;
    }

    // state.display is {0, 0} until the first CMD_DISPLAY. It is only applied
    // when CHANGED_DISPLAY is set, so the boot defaults stay until then.
    if (state.changed & mailbox::CHANGED_DISPLAY) {
        backlight::set(state.display.brightness);
        display.setRotation(state.display.rotation & 3);   // & 3: only 0..3, 4..7 would mirror
        shown = SCREEN_NONE;                 // rotated: everything has to be redrawn
    }

    const Screen next = pickScreen(state);
    if (next != shown || contentChanged(next, state.changed)) {
        drawScreen(next, state);
        shown = next;
    }
}

}  // namespace renderer
