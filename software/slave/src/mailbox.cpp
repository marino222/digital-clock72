#include "mailbox.h"

#include <string.h>
#include <pico/mutex.h>

namespace mailbox {

namespace {

State   shared;   // written by core 0, copied by core 1. Only touched under `lock`
mutex_t lock;

// Holds the lock for one block and releases it on every way out.
// Always give it a name (`Guard g;`). `Guard{};` unlocks again at the `;`.
struct Guard {
    Guard()  { mutex_enter_blocking(&lock); }
    ~Guard() { mutex_exit(&lock); }
};

// Stores `value` and sets `bit`, but only if it differs from what is stored.
// This is the filter that keeps the master's 5 s repeats from causing redraws.
// Returns true if something changed. Call with the lock held.
template <typename T>
bool store(T& slot, const T& value, uint16_t bit) {
    if (memcmp(&slot, &value, sizeof(T)) == 0) return false;
    slot = value;
    shared.changed |= bit;
    return true;
}

// The mode is set by every init, even a repeat. A repeat of the active mode
// changes nothing. The same CLOCK_INIT after a widget switches back.
void setMode(Mode mode) {
    store(shared.mode, mode, CHANGED_MODE);
}

}  // namespace

void init() {
    mutex_init(&lock);
    shared = State{};
}

bool setAngles(const protocol::ClockNodeData& angles) {
    Guard g;
    const bool notTaken = shared.changed & CHANGED_ANGLES;
    return store(shared.angles, angles, CHANGED_ANGLES) && notTaken;
}

void setClockInit(const protocol::ClockInitData& init) {
    Guard g;
    store(shared.clockInit, init, CHANGED_CLOCK_INIT);
    setMode(MODE_CLOCK);
}

void setWidgetInit(const protocol::WidgetInitData& init) {
    Guard g;
    // Only a new widget clears the fields. A repeat keeps them.
    if (store(shared.widgetInit, init, CHANGED_WIDGET_INIT)) {
        memset(shared.fields, 0, sizeof(shared.fields));
        shared.changed |= CHANGED_FIELDS;
    }
    setMode(MODE_WIDGET);
}

void setWidgetField(uint8_t fieldId, const uint8_t* data, uint8_t len) {
    if (fieldId >= protocol::MAX_WIDGET_FIELDS) return;   // parseFrame already rejects this
    if (len > protocol::MAX_WIDGET_DATA) len = protocol::MAX_WIDGET_DATA;

    // Built in full, with zeros after `len`, so the compare in store() is exact.
    WidgetField field{};
    field.len = len;
    memcpy(field.data, data, len);

    Guard g;
    store(shared.fields[fieldId], field, CHANGED_FIELDS);
}

void setDiagMode(uint8_t mode) {
    Guard g;
    store(shared.diagMode, mode, CHANGED_DIAG);
}

void setDisplay(const protocol::DisplayData& display) {
    Guard g;
    store(shared.display, display, CHANGED_DISPLAY);
}

void setAddress(const NodeAddress& address) {
    Guard g;
    store(shared.address, address, CHANGED_ADDRESS);
}

void setBadVersion(uint8_t version) {
    Guard g;
    store(shared.badVersion, version, CHANGED_VERSION);
}

bool take(State& out) {
    Guard g;
    out = shared;
    shared.changed = 0;
    return out.changed != 0;
}

}  // namespace mailbox
