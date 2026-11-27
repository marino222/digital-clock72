#pragma once

// Reads the RS485 bus and writes what this node needs into the mailbox (core 0).
//
// Every frame goes through four checks. The first one that fails drops it.
//
//   Check                   If it fails
//   1. Frame is intact      count it as a bad frame
//   2. Version matches      show the version error
//   3. Node has an address  nothing (the screen already shows "No address")
//   4. Frame is for us      nothing (target is neither our slot nor broadcast)
//
// A frame that passes goes into the mailbox. From CLOCK_UPDATE only our own
// 4 bytes are copied.
//
// The version is checked before the address. A master with the wrong version
// also breaks the ADDR chain. Checked the other way round, every node would
// only show "No address" and hide the real cause.
//
// Sequence gaps are counted after check 2, so frames for other nodes count too.

namespace bus_rx {

// Starts Serial1 at RS485_BAUD with a 1 KB receive buffer.
// DE stays low: the slave never transmits.
void begin();

// Handles the bytes that have arrived. Never waits.
void poll();

}  // namespace bus_rx
