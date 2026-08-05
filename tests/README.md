# Tests

All tests carried out are described in this directory. The tests are split into the different phases explained in the [Roadmap](../README.md#roadmap). The respective subfolders contain dedicated test code, that also may be used in the final software.

| Folder | Description |
|---|---|
| [`phase-1.1-display-performance/`](phase-1.1-display-performance/) | Single GC9A01 dev board on a Pico, target 60 FPS |
| [`phase-2.1-rs485-test-master/`](phase-2.1-rs485-test-master/) | Master code to send test-packets over RS485 |
| [`phase-2.1-rs485-test-slave/`](phase-2.1-rs485-test-slave/) | Slave code to receive and process test-packets over RS485 |
| [`phase-3.2-protocol-test-master/`](phase-3.2-protocol-test-master/) | Master code broadcasting the state protocol to all 72 nodes |
| [`phase-3.2-protocol-test-slave/`](phase-3.2-protocol-test-slave/) | Slave code receiving the broadcast and rendering its own node |

Phase 3.1 is a design phase with no code of its own; the protocol it produced lives in
[`phase-3.2-protocol-test-master/include/protocol.h`](phase-3.2-protocol-test-master/include/protocol.h).

<details>
<summary><strong>1.1 Display performance</strong></summary>

**Objective**

Drive one GC9A01 round LCD dev board from a Raspberry Pi Pico on a breadboard, sustaining 60 FPS.

![GC9A01 Test](/docs/images/GC9A01_test.jpg)

**Wiring**

| Signal | GPIO | Pin Nr.
|---|---|---|
| VCC | 3V3 (OUT)| 36 |
| GND | GND | 38|
| SCL/SCLK | GP18 | 24 |
| SDA/MOSI | GP19 | 25 |
| CS | GP20 | 26 |
| DC | GP17 | 22 |
| RST | GP16 | 21 |


**Findings**

As it turns out, actually getting 60 FPS is more difficult than anticipated. It comes down to the size of a single frame and the transfer speed of the bus. The display has 240 × 240 pixels and is set to 16-bit (2 bytes) color by default, so a single frame needs 240 × 240 × 2 = 115.2 kB of data. To achieve 60 FPS, that frame needs to be sent 60 times per second, which works out to 115.2 kB × 60 = 6912 kB/s, or 55'296 kbit/s (about 55.3 Mbit/s). Since every bit occupies one clock tick during transmission, sustaining 60 FPS requires transfer speed of at least roughly 56 MHz.

The data is sent over the SPI bus, which is wired to the peripheral clock of the RP2040. To achieve an effective transfer speed of 56 MHz, the actual peripheral clock needs to run at least double that, because SPI signals are represented as a square wave, pulling the line high and then low each take one clock tick, so sending a single bit actually costs two clock ticks. By default, the peripheral clock is wired to the USB clock, which runs at 48 MHz, which is far too slow to reach 60 FPS.

To fix this, the clock_config() function from the RP2040 SDK can be used to wire the peripheral (SPI) clock to the CPU clock instead. By default, the RP2040's system clock runs at 125 MHz, but since this project uses the earlephilhower core (set in platformio.ini), the system clock is automatically overclocked to 133 MHz. This value can be changed in platformio.ini if needed.

In the LovyanGFX config file LGFX_Config.hpp, the requested transfer speed is set via a register write (freq_write). It's important to know that the actual achievable transfer speed can only ever be the source clock divided by an even integer (2, 4, 6, …), so the frequency requested in LGFX_Config.hpp gets rounded to the nearest matching value. For example, requesting 70 MHz against a 133 MHz source clock actually yields 133 / 2 = 66.5 MHz, since dividing by 2 gives the closest match to the requested frequency.

The `freq_write` property was set to 70 MHz so as not to overwhelm the GC9A01 display controller, even though higher frequencies would probably work fine. As shown above, this rounds down to an actual SPI speed of 66.5 MHz. To sustain 60 FPS, each frame has a time budget of at most 16.6 ms to be drawn and pushed over the bus. As shown in the first calculation, a full frame's payload is 115.2 kB (921.6 kbit), which at our transfer speed of 66.5 MHz would take roughly 13.86 ms to push over SPI. That alone consumes most of the per-frame budget, and on top of pushing the data, the frame also needs to be drawn, which costs additional CPU time. So the per-frame budget splits into drawing time and push time, and together they must not exceed 16.6 ms.

Clearly the push time needed to be cut down, since the remaining 2.74 ms would never be enough to draw an entire frame. The first lever is sprites: LovyanGFX lets a frame be fully buffered in RAM and then pushed as a whole, which is much faster than drawing directly to the display. The second, bigger lever is reducing the payload itself, i.e. the actual amount of data pushed. Since only the two clock hands move, most pixels stay unchanged frame to frame, so only the pixels that actually change need to be sent. This is done by forming a bounding box around each hand's previous and next position. Tests have shown that this shrinks the payload from 115.2 kB (full frame) to ~15 kB, bringing the average push time down to 3 ms (measured) at most and leaving around 13 ms for computation.

The clock hands are drawn with `drawWedgeLine()`, which produces a clean anti-aliased (AA) line. Because of the AA, though, it's computationally expensive: it relies on floating-point math, and the RP2040 has no hardware FPU. Calling `drawWedgeLine()` twice per frame (once per hand) pushed drawing time to well over 20 ms. To bring that down, each hand is now drawn once at startup, then rotated into place every frame with `pushRotateZoom()`. Combined with the payload reduction, this pushed the frame rate to 250 FPS, far more than the display could ever show. The catch is that AA is baked in only once, in that first frame, so it looks slightly off once rotated. `pushRotateZoomWithAA()` fixes this: it keeps some AA quality while still costing much less than calling `drawWedgeLine()` fresh every frame.

**How to test**
- Build/flash the PlatformIO project in `phase-1.1-display-performance/` -> `pio run -d ./tests/phase-1.1-display-performance -t upload`
- Start serial monitor ->  `pio device monitor -d ./tests/phase-1.1-display-performance`
- The clock hands should appear and start rotating
- observe the reported FPS via serial monitor. It should be around 80 FPS.
- Occasionally a flickering can be observed. This might be due to bad signal integrity on a breadboard and will improve on a PCB.

**Results**
```bash
FPS:86.4  draw avg:8.90ms max:9.37ms  push avg:2.65ms max:3.27ms  [prev report: 1.33ms, payload: 17 kB]
FPS:89.4  draw avg:9.09ms max:9.39ms  push avg:2.07ms max:2.85ms  [prev report: 1.09ms, payload: 11 kB]
FPS:87.4  draw avg:8.84ms max:9.47ms  push avg:2.57ms max:3.27ms  [prev report: 1.06ms, payload: 19 kB]
FPS:85.8  draw avg:9.37ms max:9.77ms  push avg:2.26ms max:2.87ms  [prev report: 1.04ms, payload: 19 kB]
FPS:87.7  draw avg:8.86ms max:9.38ms  push avg:2.52ms max:3.27ms  [prev report: 1.04ms, payload: 12 kB]
FPS:89.2  draw avg:9.08ms max:9.52ms  push avg:2.10ms max:2.85ms  [prev report: 1.08ms, payload: 18 kB]
FPS:86.4  draw avg:8.88ms max:9.35ms  push avg:2.67ms max:3.27ms  [prev report: 1.03ms, payload: 20 kB]
FPS:87.3  draw avg:9.23ms max:9.80ms  push avg:2.19ms max:2.92ms  [prev report: 1.05ms, payload: 8 kB]
FPS:86.9  draw avg:9.09ms max:10.01ms  push avg:2.38ms max:3.16ms  [prev report: 1.07ms, payload: 22 kB]
FPS:86.5  draw avg:8.93ms max:9.34ms  push avg:2.61ms max:3.27ms  [prev report: 1.06ms, payload: 15 kB]
FPS:89.5  draw avg:9.10ms max:9.36ms  push avg:2.05ms max:2.86ms  [prev report: 1.01ms, payload: 14 kB]
FPS:87.2  draw avg:8.83ms max:9.44ms  push avg:2.61ms max:3.27ms  [prev report: 1.06ms, payload: 20 kB]
```

</details>


<details>
<summary><strong>2.1 RS485 link</strong></summary>

**Objective**

Test the RS485 connection by having a sender and a receiver talk to each other. The goal is to send a barebone packet via RS485 to the receiver, which then uses the received data to display something. The receiver side reuses the same display test setup as in phase 1.1.

**Wiring**

![RS485 Wiring - Sender](/docs/images/RS485_wiring_sender.PNG)
![RS485 Wiring - Receiver](/docs/images/RS485_wiring_receiver.PNG)

| Sender Module / Line | Connected Sender Pico Pin | Function  | $\leftrightarrow$ | Receiver Module / Line | Connected Receiver Pico Pin | Function |
| :--- | :--- | :--- | :---: | :--- | :--- | :--- |
| **RS485 Module (VCC)** | VBUS / 3V3 | Power Supply | **$---$** | **RS485 Module (VCC)** | VBUS / 3V3 | Power Supply |
| **RS485 Module (GND)** | Any GND Pin | Common Ground | **$---$** | **RS485 Module (GND)** | Any GND Pin | Common Ground |
| **RS485 Module (RO / RXD)** | GP13 (UART0 RX) | Data Receive | **$---$** | **RS485 Module (RO / RXD)** | GP13 (UART0 RX) | Data Receive |
| **RS485 Module (DI / TXD)** | GP12 (UART0 TX) | Data Transmit | **$---$** | **RS485 Module (DI / TXD)** | GP12 (UART0 TX) | Data Transmit |
| **RS485 Module (DE & RE)** | GP15 | Direction (HIGH = Tx) | **$---$** | **RS485 Module (DE & RE)** | GP15 | Direction (LOW = Rx) |
| *None (Sender Only)* | *N/A* | *N/A* | **$---$** | **Display (VCC / Power)** | VBUS / 3V3 | Display Power Supply |
| *None (Sender Only)* | *N/A* | *N/A* | **$---$** | **Display (GND)** | Any GND Pin | Display Ground |
| *None (Sender Only)* | *N/A* | *N/A* | **$---$** | **Display (SCL / MOSI / etc.)**| *As documented in Phase 1* | Display SPI / Control Lines |

Both Picos are connected and powered by separate USB-C cables, so each can be watched on its own serial monitor. However, for RS485 to work reliably they need to share a common ground. The sender and receiver are connected by the RS485 data lines A & B and share a GND line. The Data Enable (DE) and Read Enable (RE) pins on the RS485 module control whether it is transmitting or reading. Both are tied together to GP15 on the Pico, and the mode is set by driving that GPIO HIGH or LOW. During this test the sender always drives it HIGH (transmitting) and the receiver always drives it LOW (reading).

**Findings**

For testing, the following packet was used. It is essentially a simple C++ struct, but with a few quirks. By default, compilers add padding bytes between struct members to allow more efficient memory access. Since the sender and receiver must agree on this packet layout, it's essential that both are compiled identically. If one side ends up with different padding, the two sides disagree on the struct size and the packet can't be parsed correctly. The `#pragma pack(push, 1)` statement prevents the compiler from adding any padding, so the struct always uses the minimum number of bytes.

```cpp
#pragma pack(push, 1) 
struct ClockDataPacket {
    // --- Header ---
    uint8_t startByte1;      
    uint8_t startByte2;      
    uint32_t packetSequence; 
    
    // --- Payload ---
    char statusMessage[16]; 
    
    // --- Footer ---
    uint8_t xorChecksum;    
};
#pragma pack(pop)
```

The protocol has a fixed header and footer that stay the same regardless of the payload. The header starts with two sync bytes, always the distinct values 0xAA (10101010) and 0x55 (01010101). Written out in binary, these form a perfectly alternating bit pattern. The receiver waits for exactly this 0xAA-then-0x55 sequence before it starts reading a packet. This prevents data misalignment, and since the two values are distinct and unlikely to occur together by chance inside the payload, it forms a reliable framing marker. The `packetSequence` field increments with every packet sent, so missing or corrupted packets can be detected by gaps in the sequence.

The footer is a single `xorChecksum` byte used to verify the data arrived intact. It's calculated by running a bitwise XOR over the entire struct, excluding the checksum byte itself, independently on both the sender and the receiver. The sender stores its calculated value in the footer before transmitting, the receiver then recalculates the checksum on its end and compares the two. On a mismatch, the packet is discarded as corrupted. XOR is a simple, fast form of error detection that's sufficient for this use case. CRC-8 or CRC-16 would be more robust alternatives if needed.

As mentioned above, the receiver only starts reading the payload once it has seen both sync bytes in sequence. To make this reliable, it uses a state machine with three states: WAITING_FOR_SYNC1, WAITING_FOR_SYNC2, and READING_PAYLOAD. A state machine handles corrupted or misaligned input more robustly than nested if/else logic would.

![State machine](/docs/images/rs485_packet_state_machine.png)

This protocol design can scale to the full array, but the packet *rate* grows quickly with it. The plan for the finished clock is to broadcast a small state instruction every frame, e.g. "point the clock hand at 30°", rather than a full image. To sustain 60 FPS across all 72 nodes, that works out to 72 × 60 = 4'320 packets per second. The packet used in this test is already 31 bytes, so at that rate we'd need 4'320 × 31 B ≈ 133.9 kB/s. The baud rate sets the theoretical ceiling on the bus: at 1 Megabaud with standard 8N1 UART framing (1 start bit + 8 data bits + 1 stop bit = 10 bits per byte), the maximum payload throughput is 1'000'000 / 10 = 100 kB/s. Higher baud rates are possible, but 1 Megabaud is a reasonable target, keeping in mind the achievable rate also depends on the actual length of the RS485 line. Either way, the required 133.9 kB/s already exceeds this ~100 kB/s ceiling. With today's packet size, the bus itself is the bottleneck before we even reach 72 nodes. The physical layout should be a daisy chain, with 120 Ω termination resistors at both ends of the bus, as is standard practice for RS485.

It's clear the packet size needs to shrink for a reliable link at this scale. This is something to keep in mind for Phase 3 (proving the state protocol on breadboards). A few approaches are worth considering:
- Make the receivers smarter, so less data needs to be sent per update. Probably best avoided: it pushes more logic onto every node's firmware, which then has to be kept in sync across all of them.
- Use different packet types for different purposes: one for setting up the clock's initial state, small packets for incremental state updates, and an entirely different packet format for other applications, like a snake game.

**How to test**
- Build and flash the PlatformIO projects in `phase-2.1-rs485-test-master` and `phase-2.1-rs485-test-slave`.
- Connect the slave to the PC with a USB-C cable and open the serial monitor. It should indicate that the connection was successful.
- Connect the master to a simple power source via USB-C or a PC
- The master should start sending packets over RS485. 
- The serial monitor of the slave should display each packet that it received.
- The display should show the instructions it received over RS485


**Results**

Serial monitor shouls show something like this
```bash
RS485 Slave Initializing...
Slave ready. Listening for packets...
[SUCCESS] Pkt #354 | Uptime: 7680ms | Angles: 113.6/208.0 deg | Msg: Message
[SUCCESS] Pkt #355 | Uptime: 7696ms | Angles: 113.9/209.6 deg | Msg: Message
[SUCCESS] Pkt #356 | Uptime: 7712ms | Angles: 114.2/211.2 deg | Msg: Message
[SUCCESS] Pkt #357 | Uptime: 7728ms | Angles: 114.5/212.8 deg | Msg: Message
[SUCCESS] Pkt #358 | Uptime: 7744ms | Angles: 114.8/214.4 deg | Msg: Message
[SUCCESS] Pkt #359 | Uptime: 7760ms | Angles: 115.2/216.0 deg | Msg: Message
```

</details>

<details>
<summary><strong>3.1 protocol design</strong></summary>

**Objective**

Design the state protocol that can carry all the necessary data over the RS485 bus.

**Considerations**

We decided not to add support for generic drawing methods (e.g. for a snake game) for now, since this would add substantial complexity. Instead, priority lies on the Clock and Widget modes. To support both, it was decided not to use one fixed protocol, but to split communication into two phases: Init and Update. Init is a heavier protocol that carries configuration data, such as the color and length of the clock hands or the background image of a widget. It can be triggered by the master controller, which interrupts all other RS485 communication and thereby frees up enough bandwidth to push the heavier config data. The Update protocol, on the other hand, carries updated angles for clocks or updated values for widgets. These packets are smaller than the Init data, since the goal is to broadcast them at 60 Hz.

To keep packet sizes as small as possible, we defined four distinct protocol types: `CLOCK_INIT`, `WIDGET_INIT`, `CLOCK_UPDATE`, and `WIDGET_UPDATE`. The protocol header includes an enum, shared by master and slave, that identifies which protocol type is being transmitted.

The protocol header design was already discussed in phase 2. However, the sync bytes `0xAA` and `0x55` proposed there, generate more overhead than necessary. Our protocol therefore implements Consistent Overhead Byte Stuffing (COBS) instead. COBS works by finding every `0x00` byte in the data and replacing it with a count of how many bytes lie until the next `0x00`. It also prepends one extra byte at the start of the packet, indicating the distance to the first `0x00`. This guarantees the encoded payload never contains a `0x00` byte, so that byte can reliably be used as an end-of-packet marker. This also fixes a problem with the old method, where `0xAA` and `0x55` could still appear inside the payload by chance and cause data misalignment. COBS also has one byte less overhead than the two sync bytes it replaces. A nice property of COBS is that a single `0x00` byte marks both the end of the current packet and the start of the next one.

The XOR checksum, already implemented in phase 2, verifies that all data was transmitted correctly. If the checksum doesn't match, the receiver discards everything that follows until it sees the next `0x00`, which marks the end of the corrupted packet. So it knows the following byte starts a new packet. The header also includes the packetSequence field from phase 2, downgraded from `uint32_t` to `uint8_t` to save three bytes. It's simply a counter rolling from 0 to 255, which should be enough to detect missing packets.

As mentioned, all other RS485 communication is halted while the master sends a `CLOCK_INIT` or `WIDGET_INIT` packet, to prevent a bus overload. `CLOCK_UPDATE` packets are sent 60 times per second to ensure smooth animations. To make sure all slaves receive their data at the same time, each `CLOCK_UPDATE` packet carries the update data for all 72 nodes at once. This is far more efficient than sending individual packets, which would multiply the protocol overhead drastically. The data sits in a continuous array and is read by each slave at the position matching its address. Addresses will be assigned automatically later on. `WIDGET_UPDATE` packets, by contrast, will most likely be sent only periodically, and never target all nodes at once. For this reason, the protocol header includes a target-node byte that specifies which address a packet should be sent to.

**Frame layout**

Putting all of the above together, a frame looks like this before COBS is applied:

```
[command][targetNode][sequence][ ...payload... ][xorChecksum]
   0          1          2         3 .. N-1          N
   |______ header ______|                            |
                                              covers bytes 0..N-1
```

and this is what actually goes on the wire:

```
[ COBS( header + payload + checksum ) ][ 0x00 ]
```

Two things are worth calling out. First, there is deliberately no length field. An earlier draft
carried a 2-byte payload length, but COBS framing already tells the receiver exactly where the
frame ends, so the length is simply `decoded length - 3 header bytes - 1 checksum byte`. Sending it
as well would waste two bytes on every one of the 60 packets per second.

Second, the checksum sits at the *end* rather than in the header. That way its position is always
"the last byte of the decoded frame", which the receiver can find without first knowing which
command arrived and therefore how big the payload is meant to be.

**Making the protocol safe to build on**

The header defines the format, but a format alone does not stop the two sides drifting apart or a
corrupted packet doing damage. Four measures address that:

- **The layout is owned by code, not by convention.** `buildFrame()` and `parseFrame()` are the
  entire public API. Nothing assembles a packet by hand, so master and slave cannot disagree about
  where a field sits.
- **Size assertions.** Every packed struct is followed by a `static_assert` on its size. Phase 2.1
  noted that a padding mismatch between master and slave would be very hard to diagnose from a
  serial log; these turn it into a build error on both sides instead.
- **Payload length is validated against the command.** This matters more than it looks. Without it,
  a single corrupted command byte could make the receiver reinterpret a 33-byte widget frame as a
  288-byte `GlobalClockUpdate` and read 255 bytes past the end of its buffer. `parseFrame()` proves
  the size is right before the receiving code is allowed to cast the payload.
- **The COBS functions cannot overrun their destination.** Both take the destination capacity and
  return 0 rather than writing past it, and the decoder rejects a `0x00` code byte, which can never
  occur in a valid COBS frame.

A parse either succeeds or reports why it failed, via `ParseResult`: `PARSE_ERR_COBS` (broken
framing), `PARSE_ERR_SHORT`, `PARSE_ERR_CHECKSUM` (corrupt bytes), `PARSE_ERR_COMMAND` (unknown
command), `PARSE_ERR_LENGTH` (payload size does not match the command) or `PARSE_ERR_TARGET`. The
slave counts these and prints them once a second, which is what makes a marginal bus visible on the
bench instead of just showing up as a stuttering display.

Widget payloads are a special case, because the whole point of widgets is that their contents are
not designed yet. `WidgetUpdateData` therefore holds a worst-case 32-byte buffer, but only the bytes
actually in use are transmitted: the sender passes `1 + usedBytes` as the payload length and the
receiver recovers `usedBytes` as `payloadLen - 1`. This keeps the packet small without adding a
length field, staying consistent with the decision above.

</details>

<details>
<summary><strong>3.2 physical testing</strong></summary>

**Objective**

Prove the phase 3.1 protocol on real hardware: one master broadcasting to two slaves, both rendering
their own slot out of a single packet. Addressing is manual here; daisy-chain auto-addressing comes
later.

> **Status:** firmware is written and all three build targets compile clean. The hardware run has
> not been carried out yet, so the results block below is empty.

**Wiring**

Same RS485 wiring as phase 2.1, extended to three boards on one bus. The master and both slaves share
the A and B data lines and a common ground; each slave additionally drives its own GC9A01 over SPI,
wired exactly as in phase 1.1.

| Board | RS485 module | DE & RE (GP15) | Display |
|---|---|---|---|
| Master | A/B to the bus, common GND | HIGH (always transmitting) | none |
| Slave 0 | A/B to the bus, common GND | LOW (always receiving) | as phase 1.1 |
| Slave 1 | A/B to the bus, common GND | LOW (always receiving) | as phase 1.1 |

The bus is a daisy chain with a 120 Ω termination resistor at each physical end, as is standard for
RS485. All three boards are powered over their own USB-C cable so each can be watched on its own
serial monitor, but they must still share a common ground for the link to be reliable.

**Findings**

The test deliberately sends the real payload rather than a reduced stand-in: a full 288-byte
`GlobalClockUpdate` covering all 72 nodes, 60 times per second, exactly as the finished clock would.
A smaller test packet would sail over the bus and prove nothing about whether the design actually
fits the budget. After COBS the frame measures 295 bytes, which works out to

```
295 B x 60 Hz = 17.7 kB/s
```

against the ~100 kB/s ceiling established in phase 2.1, so the clock stream occupies roughly 18% of
the bus. That is the whole point of batching all 72 nodes into one packet: sending each node its own
packet would have meant 4'320 packets per second and, as phase 2.1 calculated, ~134 kB/s, which the
bus cannot carry. `CLOCK_INIT` (every 5 s) and `WIDGET_UPDATE` (every 2 s) are mixed into the stream
so those paths are exercised on real hardware too, not just the one hot path.

Each node is given a fixed slice of a revolution as its starting offset, so no two displays sit at
the same angle. This is what makes it visible on the bench that each slave really is reading *its
own* slot: if the addressing were broken, both displays would move in unison instead.

The slave's receive loop is markedly simpler than phase 2.1's. That version needed a three-state
machine (`WAITING_FOR_SYNC1` / `WAITING_FOR_SYNC2` / `READING_PAYLOAD`) to find the `0xAA 0x55`
marker and to handle the case where a repeated `0xAA` had to re-arm the search. With COBS there is
no such ambiguity: `0x00` cannot occur inside an encoded frame, so the loop just collects bytes until
it sees one, hands the buffer to `parseFrame()`, and resets. A receiver that gets lost mid-stream
resynchronises automatically at the very next delimiter, with no state to unwind.

The slave defers `ClockFace::begin()` until a `CLOCK_INIT` frame arrives, so the display stays blank
until the master has actually configured it. That makes the init phase observable rather than
something that silently no-ops, and since the master re-broadcasts the config every 5 s, a slave can
be reset mid-test and will recover on its own.

**How to test**

- Keep the two copies of the protocol header in sync. They are duplicated per project by design, so
  verify before every flash that they have not drifted:
  ```bash
  diff tests/phase-3.2-protocol-test-master/include/protocol.h \
       tests/phase-3.2-protocol-test-slave/include/protocol.h
  ```
  This must print nothing. A layout change that reached only one side would also be caught at build
  time by the `static_assert`s, but only if the change altered a struct size.
- Build and flash the master -> `pio run -d ./tests/phase-3.2-protocol-test-master -t upload`
- Flash the first slave as node 0 -> `pio run -d ./tests/phase-3.2-protocol-test-slave -e slave0 -t upload`
- Flash the second slave as node 1 -> `pio run -d ./tests/phase-3.2-protocol-test-slave -e slave1 -t upload`
- Open a serial monitor on each board, e.g. `pio device monitor -d ./tests/phase-3.2-protocol-test-slave`
- Each slave should report `CLOCK_INIT` within 5 seconds, then ~60 good frames per second with no
  bad frames and no sequence gaps.
- Both displays should move in lockstep off the single broadcast, but at **different** hand angles.
  Identical angles on both would mean the node addressing is being ignored.
- Exercise the error path deliberately: briefly disconnect one of the A/B lines mid-run. The slave
  should report `COBS framing error` or `checksum mismatch`, and then recover on its own once the
  line is reconnected, rather than hanging or drawing garbage. The sequence-gap counter should show
  the dropped frames.

**Results**

_Not yet measured -- pending the hardware run._

</details>