#pragma once
#include "mailbox.h"

// ADDR chain: receive, check, forward, repeat (core 0).
//
// Reads TRUNK, ROW or NONE on ADDR_IN and takes the latest valid one as its
// address. Sends its own frames every 100 ms:
//   after TRUNK(k)   ROW(k, 1, slot+1) on ADDR_OUT, TRUNK(k+1) on ADDR_ROW_OUT
//   after ROW(r, p)  ROW(r, p+1, slot+1) on ADDR_OUT
//   no address       NONE on ADDR_OUT (and on ADDR_ROW_OUT if it was driven)
// ADDR_ROW_OUT stays an input until the first TRUNK arrives.
//
// On silence the address is kept. Every change goes to mailbox::setAddress().

namespace address {

// Starts the SerialPIO receiver on ADDR_IN. The senders start when needed.
void begin();

// Handles the received bytes and sends the 100 ms repeats. Never waits.
void poll();

// The current address. bus_rx uses `slot` to pick its part of CLOCK_UPDATE
// and to filter targeted frames. Only valid if `valid` is true.
const mailbox::NodeAddress& current();

}  // namespace address
