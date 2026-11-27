#pragma once
#include <stdint.h>
#include "protocol.h"

// Hand-over between the cores (core 0 and 1).
//
// Holds the current state, not messages. Core 0 applies every message with a
// set...() call. Core 1 copies the whole state with take(). A newer value
// replaces an older one that was not drawn yet. Nothing is lost, because every
// value is complete on its own.

namespace mailbox {

// What the last init switched to.
enum Mode : uint8_t {
    MODE_NONE   = 0,    // no init yet
    MODE_CLOCK  = 1,
    MODE_WIDGET = 2
};

// One bit per entry of State. Set by set...() only if the value differs from
// the stored one, so the master's 5 s repeats cause no redraw. Cleared by take().
enum Changed : uint16_t {
    CHANGED_ANGLES      = 1 << 0,
    CHANGED_CLOCK_INIT  = 1 << 1,
    CHANGED_WIDGET_INIT = 1 << 2,
    CHANGED_FIELDS      = 1 << 3,
    CHANGED_MODE        = 1 << 4,
    CHANGED_DIAG        = 1 << 5,
    CHANGED_ADDRESS     = 1 << 6,
    CHANGED_VERSION     = 1 << 7,
    CHANGED_DISPLAY     = 1 << 8
};

// The node's address. Written by `address`, shown on the screen.
struct NodeAddress {
    bool    valid;
    uint8_t row;
    uint8_t pos;
    uint8_t slot;
};

// Latest value of one widget field.
struct WidgetField {
    uint8_t len;                    // 0 = not received yet
    uint8_t data[protocol::MAX_WIDGET_DATA];
};

struct State {
    protocol::ClockNodeData  angles;       // own slot of the last CLOCK_UPDATE
    protocol::ClockInitData  clockInit;
    protocol::WidgetInitData widgetInit;
    protocol::DisplayData    display;      // rotation and brightness. {0, 0} until the first CMD_DISPLAY
    WidgetField              fields[protocol::MAX_WIDGET_FIELDS];   // cleared by every new WIDGET_INIT
    Mode                     mode;         // set by the last init
    uint8_t                  diagMode;     // protocol::DiagnosticMode
    NodeAddress              address;
    uint8_t                  badVersion;   // 0 = ok, otherwise the received version
    uint16_t                 changed;      // Changed bits since the last take()
};

// Sets up the lock. Called once by main.cpp on core 0, before the ready flag
// releases core 1. Not a begin(): it belongs to neither core's loop, and core 1
// starts before setup(), so it must not touch the mailbox earlier.
void init();

// --- Core 0 ---

// Returns true if the previous angles were never taken (core 1 fell behind).
bool setAngles(const protocol::ClockNodeData& angles);
void setClockInit(const protocol::ClockInitData& init);     // also sets mode = clock
void setWidgetInit(const protocol::WidgetInitData& init);   // also sets mode = widget, clears fields
void setWidgetField(uint8_t fieldId, const uint8_t* data, uint8_t len);
void setDiagMode(uint8_t mode);
void setDisplay(const protocol::DisplayData& display);
void setAddress(const NodeAddress& address);
void setBadVersion(uint8_t version);                // 0 clears it, on the next valid frame

// --- Core 1 ---

// Copies the whole state into `out` and clears `changed`.
// Returns false if nothing changed since the last take().
bool take(State& out);

}  // namespace mailbox
