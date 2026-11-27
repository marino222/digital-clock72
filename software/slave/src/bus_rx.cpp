#include "bus_rx.h"     // own interface

#include <Arduino.h>    // Serial1, pinMode
#include <string.h>     // memcpy
#include "board.h"      // PIN_RS485_*
#include "address.h"    // address::current()
#include "mailbox.h"    // mailbox::set...()

namespace bus_rx {

namespace {

// Receiving: the same as in address, only bigger (a CLOCK_UPDATE is 296 bytes).
uint8_t rxBuf[protocol::MAX_ENCODED_FRAME];
size_t  rxLen      = 0;
bool    rxOverflow = false;             // frame too long: drop it up to the next 0x00

// Counters. Private for now. They go to diagnostics once it exists.
uint32_t framesOk   = 0;   // valid frames
uint32_t framesBad  = 0;   // noise: checksum, COBS, ...
uint32_t seqGaps    = 0;   // gaps in the sequence counter = frames lost on the bus
uint32_t superseded = 0;   // angles that core 1 never drew

uint8_t lastSequence     = 0;
bool    haveLastSequence = false;   // no frame yet, so nothing to compare against

void handleFrame(const uint8_t* frame, size_t len);   // defined below

// Reads only the bytes that are already there. Never waits.
// Same as address.cpp. Keep both in sync.
void receive() {
    while (Serial1.available()) {
        const uint8_t b = Serial1.read();

        if (b == protocol::FRAME_DELIMITER) {     // 0x00: frame complete
            if (rxLen > 0 && !rxOverflow) {
                handleFrame(rxBuf, rxLen);
            }
            rxLen = 0;
            rxOverflow = false;
            continue;
        }

        if (rxLen < sizeof(rxBuf)) {
            rxBuf[rxLen++] = b;
        } else {
            rxOverflow = true;                    // longer than any valid frame
        }
    }
}

// The master numbers every frame 0, 1, 2 ... 255, 0, 1 ...
// A jump means frames were lost on the bus.
void countSequence(uint8_t seq) {
    if (haveLastSequence && seq != uint8_t(lastSequence + 1)) {   // uint8_t(): 255 + 1 wraps to 0
        seqGaps++;
    }
    lastSequence     = seq;
    haveLastSequence = true;
}

// Writes one checked frame into the mailbox.
// parseFrame has already checked every payload size, so the copies cannot overrun.
// The payload is copied because f.payload points into scratch, which ends with handleFrame().
void apply(const protocol::Frame& f, uint8_t slot) {
    switch (f.header.command) {
        case protocol::CMD_CLOCK_INIT: {
            protocol::ClockInitData d;
            memcpy(&d, f.payload, sizeof(d));
            mailbox::setClockInit(d);
            break;
        }
        case protocol::CMD_WIDGET_INIT: {
            protocol::WidgetInitData d;
            memcpy(&d, f.payload, sizeof(d));
            mailbox::setWidgetInit(d);
            break;
        }
        case protocol::CMD_CLOCK_UPDATE: {
            // All 72 nodes in one frame, 4 bytes each. Ours starts at slot × 4.
            protocol::ClockNodeData d;
            memcpy(&d, f.payload + slot * sizeof(d), sizeof(d));
            if (mailbox::setAngles(d)) superseded++;   // true: core 1 missed the previous angles
            break;
        }
        case protocol::CMD_WIDGET_UPDATE:
            // Payload: fieldId, then only the used data bytes.
            mailbox::setWidgetField(f.payload[0], f.payload + 1, uint8_t(f.payloadLen - 1));
            break;
        case protocol::CMD_DIAGNOSTIC:
            mailbox::setDiagMode(f.payload[0]);
            break;
        case protocol::CMD_DISPLAY: {
            protocol::DisplayData d;
            memcpy(&d, f.payload, sizeof(d));
            mailbox::setDisplay(d);
            break;
        }
    }
}

// Runs the four checks from bus_rx.h. A frame that passes goes to apply().
void handleFrame(const uint8_t* frame, size_t len) {
    uint8_t         scratch[protocol::MAX_DECODED_FRAME];   // f.payload points in here
    protocol::Frame f;

    const protocol::ParseResult r = protocol::parseFrame(frame, len, scratch, sizeof(scratch), &f);

    // Check 2: version. Asked first because it needs its own reaction.
    // parseFrame checks it after the checksum, so the frame itself is intact.
    if (r == protocol::PARSE_ERR_VERSION) {
        mailbox::setBadVersion(f.header.version);   // screen: "Protocol vX, expected vY"
        return;
    }

    // Check 1: intact. Every other error is noise.
    if (r != protocol::PARSE_OK) {
        framesBad++;
        return;
    }

    framesOk++;
    mailbox::setBadVersion(0);                      // valid frame: clear a version error
    countSequence(f.header.sequence);               // before the filters: all frames share the counter

    // Check 3: address.
    const mailbox::NodeAddress& me = address::current();
    if (!me.valid) return;

    // Check 4: for us?
    if (f.header.targetNode != protocol::GLOBAL_BROADCAST_ID &&
        f.header.targetNode != me.slot) return;

    apply(f, me.slot);
}

}  // namespace

void begin() {
    pinMode(PIN_RS485_DE, OUTPUT);
    digitalWrite(PIN_RS485_DE, LOW);    // driver off: the slave never transmits
    pinMode(PIN_RS485_RE, OUTPUT);
    digitalWrite(PIN_RS485_RE, LOW);    // receiver on (active low)

    Serial1.setTX(PIN_RS485_TX);
    Serial1.setRX(PIN_RS485_RX);
    Serial1.setFIFOSize(1024);          // must come before begin(), otherwise it is ignored
    Serial1.begin(protocol::RS485_BAUD);
}

void poll() {
    receive();
}

}  // namespace bus_rx
