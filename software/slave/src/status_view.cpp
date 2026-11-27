#include "status_view.h"    // own interface

#include <stdio.h>          // snprintf
#include "LGFX_Config.hpp"  // LGFX

namespace status_view {

namespace {

LGFX* screen = nullptr;     // set by begin(). A pointer, because it is filled in later

// Fills the whole screen with `bg` and draws `count` lines of text in the middle.
void drawTextScreen(const char* const lines[], int count, uint16_t fg, uint16_t bg) {
    screen->fillScreen(bg);
    screen->setFont(&fonts::Font4);              // 26 px, as for the widget text in 3.2
    screen->setTextColor(fg, bg);
    screen->setTextDatum(middle_center);         // x/y below mean "the centre of the text"

    const int lineHeight = screen->fontHeight() + 4;
    int y = screen->height() / 2 - (count - 1) * lineHeight / 2;   // the block sits in the middle
    for (int i = 0; i < count; i++) {
        screen->drawString(lines[i], screen->width() / 2, y);
        y += lineHeight;
    }
}

}  // namespace

void begin(LGFX& display) {
    screen = &display;
}

void drawVersionError(uint8_t received) {
    char a[20], b[20];
    snprintf(a, sizeof(a), "Protocol v%u", received);
    snprintf(b, sizeof(b), "expected v%u", protocol::PROTOCOL_VERSION);
    const char* lines[] = {a, b};
    drawTextScreen(lines, 2, TFT_WHITE, TFT_RED);   // red screen: see software/README.md
}

void drawNoAddress() {
    const char* lines[] = {"No address"};
    drawTextScreen(lines, 1, TFT_RED, TFT_BLACK);
}

void drawAddress(const mailbox::NodeAddress& address) {
    // Three lines: the "·" of "Row r · Pos p · Slot s" is not in the built-in
    // fonts, and one line would be too wide for the round display.
    char a[20], b[20], c[20];
    snprintf(a, sizeof(a), "Row %u",  address.row);
    snprintf(b, sizeof(b), "Pos %u",  address.pos);
    snprintf(c, sizeof(c), "Slot %u", address.slot);
    const char* lines[] = {a, b, c};
    drawTextScreen(lines, 3, TFT_WHITE, TFT_BLACK);
}

void drawWaiting() {
    const char* lines[] = {"..."};
    drawTextScreen(lines, 1, TFT_WHITE, TFT_BLACK);
}

}  // namespace status_view
