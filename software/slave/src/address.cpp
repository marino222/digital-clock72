#include "address.h"     // own interface. Pulls in mailbox.h and protocol.h

#include <Arduino.h>     // SerialPIO, NOPIN
#include <string.h>      // memcmp, memcpy
#include "board.h"       // PIN_ADDR_*

namespace address {

namespace {

// Software UARTs (PIO), because the ADDR pins are not hardware UART pins and
// one node needs one input and two outputs. Creating them does not touch the
// pins yet. Only begin() does.
SerialPIO addrIn    (NOPIN,            PIN_ADDR_IN);   // receives only
SerialPIO addrOut   (PIN_ADDR_OUT,     NOPIN);         // sends only
SerialPIO addrRowOut(PIN_ADDR_ROW_OUT, NOPIN);         // sends only, first node of a row only

mailbox::NodeAddress myAddress{};   // {} sets all 0, so valid = false until the first frame

// Receiving: bytes are collected here until the 0x00 at the end of a frame.
uint8_t rxBuf[protocol::MAX_ADDR_ENCODED_FRAME];
size_t  rxLen      = 0;
bool    rxOverflow = false;             // frame too long: drop it up to the next 0x00

// Sending: one ready-encoded frame per sender, and how far it has been sent.
// Frames stay here inside address. Only the address itself goes to the mailbox.
struct Outgoing {
    uint8_t buf[protocol::MAX_ADDR_ENCODED_FRAME];
    size_t  len  = 0;                  // 0 = send nothing
    size_t  sent = 0;                  // bytes of buf already written
};
Outgoing toOut;                        // for addrOut
Outgoing toRowOut;                     // for addrRowOut
bool     rowOutStarted = false;        // ADDR_ROW_OUT stays an input until the first TRUNK

// Copies a newly built frame into `o`, the box that send() reads from.
// An identical frame is ignored, so a half-sent frame is never cut off.
void update(Outgoing& o, const uint8_t* frame, size_t len) {
    if (len == o.len && memcmp(frame, o.buf, len) == 0) return;

    memcpy(o.buf, frame, len);
    o.len  = len;
    o.sent = 0;                        // start from the first byte: send immediately
}

// Starts driving ADDR_ROW_OUT. Only the first node of a row ever does this.
void startRowOut() {
    if (rowOutStarted) return;
    addrRowOut.begin(protocol::ADDR_BAUD);
    rowOutStarted = true;
}

// Sends NONE on ADDR_ROW_OUT, so the next row drops its outdated address.
void dropRowOut() {
    uint8_t tmp[protocol::MAX_ADDR_ENCODED_FRAME];
    update(toRowOut, tmp, protocol::buildAddrNone(tmp, sizeof(tmp)));
}

// Takes the address from the frame and builds the frames for the next nodes.
void takeAddress(const protocol::AddrFrame& f) {
    myAddress = {true, f.row, f.pos, f.slot};
    mailbox::setAddress(myAddress);              // only the address goes to the mailbox, for the screen

    uint8_t tmp[protocol::MAX_ADDR_ENCODED_FRAME];
    size_t  len;

    // Along the row: the next node is one pos and one slot further.
    const protocol::AddrRowData next{f.row, uint8_t(f.pos + 1), uint8_t(f.slot + 1), f.rowLength};
    len = protocol::buildAddrRow(next, tmp, sizeof(tmp));
    update(toOut, tmp, len);

    if (f.type == protocol::ADDR_TRUNK) {
        // Back into the trunk: the first node of the next row.
        protocol::AddrTrunkData trunk{};
        trunk.row      = f.row + 1;
        trunk.rowCount = f.rowCount;
        memcpy(trunk.rowLengths, f.rowLengths, f.rowCount);   // copy now: f.rowLengths points into scratch
        len = protocol::buildAddrTrunk(trunk, tmp, sizeof(tmp));
        startRowOut();
        update(toRowOut, tmp, len);
    } else if (rowOutStarted) {
        dropRowOut();                            // used to be first in a row, is not any more
    }
}

// No valid address: tell the screen, and send NONE so the nodes behind us drop theirs too.
void dropAddress() {
    myAddress = {};                              // all 0, so valid = false
    mailbox::setAddress(myAddress);

    uint8_t tmp[protocol::MAX_ADDR_ENCODED_FRAME];
    const size_t len = protocol::buildAddrNone(tmp, sizeof(tmp));
    update(toOut, tmp, len);
    if (rowOutStarted) update(toRowOut, tmp, len);
}

// Writes as much of `o` as the sender's FIFO takes right now. Never waits.
// SerialPIO::write() would block once its 8-byte FIFO is full.
void sendSome(SerialPIO& port, Outgoing& o) {
    while (o.sent < o.len && port.availableForWrite() > 0) {
        port.write(o.buf[o.sent++]);
    }
}

uint32_t lastRepeatMs = 0;

// Sends the frames, and starts them again every ADDR_REPEAT_MS.
void send() {
    const uint32_t now = millis();
    if (now - lastRepeatMs >= protocol::ADDR_REPEAT_MS) {   // written this way, it survives the millis() wrap
        lastRepeatMs = now;
        if (toOut.sent    == toOut.len)    toOut.sent    = 0;   // only a finished frame starts again
        if (toRowOut.sent == toRowOut.len) toRowOut.sent = 0;
    }
    sendSome(addrOut, toOut);
    if (rowOutStarted) sendSome(addrRowOut, toRowOut);
}

void handleFrame(const uint8_t* frame, size_t len);   // defined below

// Reads only the bytes that are already there. Never waits.
// Same as bus_rx.cpp. Keep both in sync.
void receive() {
    while (addrIn.available()) {
        const uint8_t b = addrIn.read();

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

// Decides what a received frame means for the address.
void handleFrame(const uint8_t* frame, size_t len) {
    uint8_t   scratch[protocol::MAX_ADDR_DECODED_FRAME];   // decode workspace. f.rowLengths points in here
    protocol::AddrFrame f;

    const protocol::ParseResult r = protocol::parseAddrFrame(frame, len, scratch, sizeof(scratch), &f);

    switch (r) {
        case protocol::PARSE_OK:
            if (f.type == protocol::ADDR_NONE) dropAddress();
            else                               takeAddress(f);
            break;

        case protocol::PARSE_ERR_VERSION:        // configuration error:
        case protocol::PARSE_ERR_LAYOUT:         // the address would be wrong
            dropAddress();
            break;

        default:                                 // noise (checksum, COBS, ...):
            break;                               // keep the address, the next repeat comes in 100 ms
    }
}

}  // namespace

void begin() {
    addrIn.begin(protocol::ADDR_BAUD);
    addrOut.begin(protocol::ADDR_BAUD);
    // addrRowOut is not started here: ADDR_ROW_OUT stays an input until a TRUNK arrives.
}

void poll() {
    receive();
    send();
}

const mailbox::NodeAddress& current() {
    return myAddress;
}

}  // namespace address
