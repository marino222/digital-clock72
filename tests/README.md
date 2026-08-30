# Tests

All tests carried out are described in this directory. The tests are split into the different phases explained in the [Roadmap](../README.md#roadmap). The respective subfolders contain dedicated test code, that also may be used in the final software.

| Folder | Description |
|---|---|
| [`phase-1.1-display-performance/`](phase-1.1-display-performance/) | Single GC9A01 dev board on a Pico, target 60 FPS |
| [`phase-2.1-rs485-test-master/`](phase-2.1-rs485-test-master/) | Master code to send test-packets over RS485 |
| [`phase-2.1-rs485-test-slave/`](phase-2.1-rs485-test-slave/) | Slave code to receive and process test-packets over RS485 |
| [`phase-3.1-protocol-design/`](phase-3.1-protocol-design/) | Protocol definitions and unit test code |
| [`phase-3.2-protocol-test-master/`](phase-3.2-protocol-test-master/) | Master code broadcasting the state protocol to all 72 nodes |
| [`phase-3.2-protocol-test-slave/`](phase-3.2-protocol-test-slave/) | Slave code receiving the broadcast and rendering its own node |


<details>
<summary><strong>1.1 Display performance</strong></summary>

### Objective

Drive one GC9A01 round LCD dev board from a Raspberry Pi Pico on a breadboard, sustaining 60 FPS.

![GC9A01 Test](/docs/images/GC9A01_test.jpg)

### Wiring

| Signal | GPIO | Pin Nr.
|---|---|---|
| VCC | 3V3 (OUT)| 36 |
| GND | GND | 38|
| SCL/SCLK | GP18 | 24 |
| SDA/MOSI | GP19 | 25 |
| CS | GP20 | 26 |
| DC | GP17 | 22 |
| RST | GP16 | 21 |


### Findings

As it turns out, actually getting 60 FPS is more difficult than anticipated. It comes down to the size of a single frame and the transfer speed of the bus. The display has 240 × 240 pixels and is set to 16-bit (2 bytes) color by default, so a single frame needs 240 × 240 × 2 = 115.2 kB of data. To achieve 60 FPS, that frame needs to be sent 60 times per second, which works out to 115.2 kB × 60 = 6912 kB/s, or 55'296 kbit/s (about 55.3 Mbit/s). Since every bit occupies one clock tick during transmission, sustaining 60 FPS requires transfer speed of at least roughly 56 MHz.

The data is sent over the SPI bus, which is wired to the peripheral clock of the RP2040. To achieve an effective transfer speed of 56 MHz, the actual peripheral clock needs to run at least double that, because SPI signals are represented as a square wave, pulling the line high and then low each take one clock tick, so sending a single bit actually costs two clock ticks. By default, the peripheral clock is wired to the USB clock, which runs at 48 MHz, which is far too slow to reach 60 FPS.

To fix this, the clock_config() function from the RP2040 SDK can be used to wire the peripheral (SPI) clock to the CPU clock instead. By default, the RP2040's system clock runs at 125 MHz, but since this project uses the earlephilhower core (set in platformio.ini), the system clock is automatically overclocked to 133 MHz. This value can be changed in platformio.ini if needed.

In the LovyanGFX config file LGFX_Config.hpp, the requested transfer speed is set via a register write (freq_write). It's important to know that the actual achievable transfer speed can only ever be the source clock divided by an even integer (2, 4, 6, …), so the frequency requested in LGFX_Config.hpp gets rounded to the nearest matching value. For example, requesting 70 MHz against a 133 MHz source clock actually yields 133 / 2 = 66.5 MHz, since dividing by 2 gives the closest match to the requested frequency.

The `freq_write` property was set to 70 MHz so as not to overwhelm the GC9A01 display controller, even though higher frequencies would probably work fine. As shown above, this rounds down to an actual SPI speed of 66.5 MHz. To sustain 60 FPS, each frame has a time budget of at most 16.6 ms to be drawn and pushed over the bus. As shown in the first calculation, a full frame's payload is 115.2 kB (921.6 kbit), which at our transfer speed of 66.5 MHz would take roughly 13.86 ms to push over SPI. That alone consumes most of the per-frame budget, and on top of pushing the data, the frame also needs to be drawn, which costs additional CPU time. So the per-frame budget splits into drawing time and push time, and together they must not exceed 16.6 ms.

Clearly the push time needed to be cut down, since the remaining 2.74 ms would never be enough to draw an entire frame. The first lever is sprites: LovyanGFX lets a frame be fully buffered in RAM and then pushed as a whole, which is much faster than drawing directly to the display. The second, bigger lever is reducing the payload itself, i.e. the actual amount of data pushed. Since only the two clock hands move, most pixels stay unchanged frame to frame, so only the pixels that actually change need to be sent. This is done by forming a bounding box around each hand's previous and next position. Tests have shown that this shrinks the payload from 115.2 kB (full frame) to ~15 kB, bringing the average push time down to 3 ms (measured) at most and leaving around 13 ms for computation.

The clock hands are drawn with `drawWedgeLine()`, which produces a clean anti-aliased (AA) line. Because of the AA, though, it's computationally expensive: it relies on floating-point math, and the RP2040 has no hardware FPU. Calling `drawWedgeLine()` twice per frame (once per hand) pushed drawing time to well over 20 ms. To bring that down, each hand is now drawn once at startup, then rotated into place every frame with `pushRotateZoom()`. Combined with the payload reduction, this pushed the frame rate to 250 FPS, far more than the display could ever show. The catch is that AA is baked in only once, in that first frame, so it looks slightly off once rotated. `pushRotateZoomWithAA()` fixes this: it keeps some AA quality while still costing much less than calling `drawWedgeLine()` fresh every frame.

### How to test
- Build/flash the PlatformIO project in `phase-1.1-display-performance/` -> `pio run -d ./tests/phase-1.1-display-performance -t upload`
- Start serial monitor ->  `pio device monitor -d ./tests/phase-1.1-display-performance`
- The clock hands should appear and start rotating
- observe the reported FPS via serial monitor. It should be around 80 FPS.
- Occasionally a flickering can be observed. This might be due to bad signal integrity on a breadboard and will improve on a PCB.

### Results
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
<summary><strong>1.2 power measurement</strong></summary>

### Objective

Measure how much power a Raspberry Pi Pico with an attached GC9A01 display and RS485 communication draws

### Results

The following average values were recorded:
- 5.1 V
- 0.06 A
- 0.3 W

</details>


<details>
<summary><strong>2.1 RS485 link</strong></summary>

### Objective

Test the RS485 connection by having a sender and a receiver talk to each other. The goal is to send a barebone packet via RS485 to the receiver, which then uses the received data to display something. The receiver side reuses the same display test setup as in phase 1.1.

### Wiring

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

### Findings

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

### How to test
- Build and flash the PlatformIO projects in `phase-2.1-rs485-test-master` and `phase-2.1-rs485-test-slave`.
- Connect the slave to the PC with a USB-C cable and open the serial monitor. It should indicate that the connection was successful.
- Connect the master to a simple power source via USB-C or a PC
- The master should start sending packets over RS485. 
- The serial monitor of the slave should display each packet that it received.
- The display should show the instructions it received over RS485


### Results

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

### Objective

Design the state protocol that can carry all the necessary data over the RS485 bus.

### Considerations

It was decided not to add support for generic drawing methods (e.g. for a snake game) for now, since this would add substantial complexity. Instead, priority lies on the Clock and Widget modes. To support both, it was decided not to use one fixed protocol, but to split communication into two phases: Init and Update. Init is a heavier protocol that carries configuration data, such as the color and length of the clock hands or the background image of a widget. It can be triggered by the master controller, which interrupts all other RS485 communication and thereby frees up enough bandwidth to push the heavier config data. The Update protocol, on the other hand, carries updated angles for clocks or updated values for widgets. These packets are smaller than the Init data, since the goal is to broadcast them at 60 Hz.

To keep packet sizes as small as possible, I defined four distinct protocol types: `CLOCK_INIT`, `WIDGET_INIT`, `CLOCK_UPDATE`, and `WIDGET_UPDATE`. The protocol header includes an enum, shared by master and slave, that identifies which protocol type is being transmitted.

The protocol header design was already discussed in phase 2. However, the sync bytes `0xAA` and `0x55` proposed there, generate more overhead than necessary. Our protocol therefore implements Consistent Overhead Byte Stuffing (COBS) instead. COBS works by finding every `0x00` byte in the data and replacing it with a count of how many bytes lie until the next `0x00`. It also prepends one extra byte at the start of the packet, indicating the distance to the first `0x00`. This guarantees the encoded payload never contains a `0x00` byte, so that byte can reliably be used as an end-of-packet marker. This also fixes a problem with the old method, where `0xAA` and `0x55` could still appear inside the payload by chance and cause data misalignment. COBS also has one byte less overhead than the two sync bytes it replaces. A nice property of COBS is that a single `0x00` byte marks both the end of the current packet and the start of the next one.

The XOR checksum, already implemented in phase 2, verifies that all data was transmitted correctly. If the checksum doesn't match, the receiver discards everything that follows until it sees the next `0x00`, which marks the end of the corrupted packet. So it knows the following byte starts a new packet. The header also includes the packetSequence field from phase 2, downgraded from `uint32_t` to `uint8_t` to save three bytes. It's simply a counter rolling from 0 to 255, which should be enough to detect missing packets.

As mentioned, all other RS485 communication is halted while the master sends a `CLOCK_INIT` or `WIDGET_INIT` packet, to prevent a bus overload. `CLOCK_UPDATE` packets are sent 60 times per second to ensure smooth animations. To make sure all slaves receive their data at the same time, each `CLOCK_UPDATE` packet carries the update data for all 72 nodes at once. This is far more efficient than sending individual packets, which would multiply the protocol overhead drastically. The data sits in a continuous array and is read by each slave at the position matching its address. Addresses will be assigned automatically later on. `WIDGET_UPDATE` packets, by contrast, will most likely be sent only periodically, and never target all nodes at once. For this reason, the protocol header includes a target-node byte that specifies which address a packet should be sent to.

### Frame Layout

Putting all of the above together, a frame looks like this before COBS is applied:

```
[command][targetNode][sequence][ ...payload... ][xorChecksum]
   0          1          2         3 .. N-1          N
   |______ header ______|                            |
                                              covers bytes 0..N-1
```

To safely transmit this over the RS485 bus, the entire structure is COBS-encoded and terminated with a single `0x00` delimiter:

```
[ COBS( header + payload + checksum ) ][ 0x00 ]
```

Every part of a frame is described by a C++ struct in `protocol.h`. Master and slave are compiled for different devices, so each struct is wrapped in `#pragma pack(push, 1)`. This stops the compiler from adding padding bytes between the members and keeps the layout identical on both ends. Each packed struct is also followed by a `static_assert()` on its size. If a struct ever ends up a different size than expected, the code simply won't compile.

`protocol.h` offers two functions as its public API: `buildFrame()` and `parseFrame()`. Master and slave only ever need these two. Everything else in the file (COBS, checksum, structs) is just the machinery behind them.

The master calls `buildFrame()` and passes the following arguments:
- `cmd`: which type of packet to build, taken from the `CommandType` enum.
- `targetNode`: which slave the packet is meant for. Usually `0xFF` (`GLOBAL_BROADCAST_ID`) to address all nodes at once. Realistically only `CMD_WIDGET_UPDATE` needs a specific address, in the range 0..71.
- `sequence`: a counter the master increments with every packet, so the slave can spot gaps and detect dropped packets.
- `payload`: a pointer to the payload. The master builds it from the structs in `protocol.h` and passes the address. May be `nullptr` if `payloadLen` is 0.
- `payloadLen`: how many bytes of the payload to send. Most commands have a fixed size, so this is usually redundant, but `CMD_WIDGET_UPDATE` has a variable payload and needs it. Anything above `MAX_PAYLOAD_SIZE` (288 bytes) is rejected.
- `out`: the buffer to write the finished frame to.
- `outCap`: how big `out` is, purely to prevent an overflow. `MAX_ENCODED_FRAME` is always big enough.

`buildFrame()` puts together the header, payload and checksum, COBS-encodes the whole thing, appends the `0x00` delimiter and writes it to `out`. It returns how many bytes it wrote (delimiter included), or 0 if it failed. The master sends exactly that many bytes down the wire.

The slave calls `parseFrame()`, which does the opposite:
- `encoded`: a pointer to the encoded frame read from the bus, *without* the trailing `0x00`. The slave strips the delimiter before calling.
- `encodedLen`: how many bytes were read from the bus.
- `scratch`: a buffer owned by the slave that the frame is decoded into. The result points into this buffer instead of copying, so it has to stay untouched for as long as the frame is used.
- `scratchCap`: how big `scratch` is, again to prevent an overflow. `MAX_DECODED_FRAME` is always big enough.
- `out`: a `Frame` struct that gets filled in on success, giving the slave the header plus a pointer and length for the payload. It stays untouched if parsing fails.

`parseFrame()` doesn't just report success or failure. It returns a `ParseResult` naming the step that rejected the frame: COBS framing, minimum length, checksum, unknown command, payload length or target node. The checks run in that order, so the first one to fail is also the most basic problem. `parseResultName()` turns the result into readable text for the serial log.

### Unit tests

To test the protocol, I let Claude generate some unit tests. `test/test_protocol.cpp` holds 17 [Unity](https://github.com/ThrowTheSwitch/Unity) tests covering `protocol.h`, split into three groups.

**Round trips** build a frame and parse it straight back, checking that every field survives. If these pass, the encoder, checksum, COBS and validation all agree with each other. Three payloads are worth calling out:
- An all-zero `CLOCK_UPDATE`. Every byte needs escaping, which is the hardest test of the encoder's zero handling.
- A `CLOCK_UPDATE` with no zero bytes at all. COBS can only cover 254 bytes per block, so this payload is long enough to force the encoder to start a second one. It also produces the biggest frame the protocol can ever emit, exactly `MAX_ENCODED_FRAME` (295 bytes). Zeros are the *cheap* case for COBS, so it is a payload without any that costs the most, not an all-zero one.
- A short `WIDGET_UPDATE`, confirming the receiver recovers the used length rather than the full struct size.

**Rejection paths** feed `parseFrame()` deliberately broken frames and check that it fails for the *right* reason, not just that it fails. There is one test per `ParseResult`: a corrupted payload byte, an unknown command, a frame too short to hold a header, an out-of-range target node, a wrong payload length, and two malformed COBS streams.

**Caller-error paths** cover the guards that stop a bad call from corrupting memory: an oversized payload, a null payload, and output/scratch buffers that are too small. All of these must return 0 or an error instead of writing past the end of a buffer.

### How to run the unit tests

There are two environments in `platformio.ini`. The native one runs on the PC and needs no hardware:

```bash
pio test -d ./tests/phase-3.1-protocol-design -e native
```

The same suite can also run on a real Pico. This is slower and needs a board attached over USB, but it is the only way to confirm how the packed structs actually behave on the RP2040:

```bash
pio test -d ./tests/phase-3.1-protocol-design -e pico
```

### Unit test results

```bash
test/test_protocol.cpp:321: test_clock_init_round_trip	[PASSED]
test/test_protocol.cpp:322: test_all_zero_payload_round_trip	[PASSED]
test/test_protocol.cpp:323: test_cobs_worst_case_round_trip	[PASSED]
test/test_protocol.cpp:324: test_widget_update_variable_length	[PASSED]
test/test_protocol.cpp:325: test_widget_update_length_bounds	[PASSED]
test/test_protocol.cpp:328: test_corrupted_payload_fails_checksum	[PASSED]
test/test_protocol.cpp:329: test_unknown_command_rejected	[PASSED]
test/test_protocol.cpp:330: test_short_frame_rejected	[PASSED]
test/test_protocol.cpp:331: test_invalid_target_rejected	[PASSED]
test/test_protocol.cpp:332: test_valid_targets_accepted	[PASSED]
test/test_protocol.cpp:333: test_payload_length_mismatch_rejected	[PASSED]
test/test_protocol.cpp:334: test_zero_code_byte_rejected	[PASSED]
test/test_protocol.cpp:335: test_truncated_cobs_block_rejected	[PASSED]
test/test_protocol.cpp:338: test_oversized_payload_refused	[PASSED]
test/test_protocol.cpp:339: test_null_payload_refused	[PASSED]
test/test_protocol.cpp:340: test_build_refuses_small_out_buffer	[PASSED]
test/test_protocol.cpp:341: test_parse_refuses_small_scratch	[PASSED]
--------------------- native:* [PASSED] Took 0.64 seconds ---------------------

=================================== SUMMARY ===================================
Environment    Test    Status    Duration
-------------  ------  --------  ------------
native         *       PASSED    00:00:00.639
================= 17 test cases: 17 succeeded in 00:00:00.639 =================
```

### Frame inspector

To see what the generated and encoded data actually looks like, Claude built a script in `src/main.cpp` that builds a frame and prints what it holds at every step along the way.

`buildFrame()` deliberately does the raw assembly and the COBS encoding in one call, so the intermediate stages can't be seen through the public API. The inspector walks the same pipeline one stage at a time, using the same `protocol.h` helpers the real master and slave use:

| Stage | Shows |
|---|---|
| 1. Payload | The struct as the master filled it, as hex and as decoded fields |
| 2. Raw frame | Header, payload and checksum, with the meaning of each byte |
| 3. COBS encoded | Which bytes are code bytes and which are data, and what the encoding cost |
| 4. On the wire | The exact bytes `Serial1.write()` would push, plus what they cost on the bus |
| 5. Received | Delimiter stripping and COBS decode, compared back against stage 2 |
| 6. Parsed | The `ParseResult`, the header, and the payload decoded field by field |

There is also an optional fault injection switch that corrupts the frame after encoding, so it can be seen which layer catches the damage.

### How to run the inspector

The inspector is host-only and needs no hardware:

```bash
pio run -d ./tests/phase-3.1-protocol-design -e native -t exec
```

### Inspector output

A run for a `CMD_CLOCK_INIT` frame addressed to node 5. Stages 1 and 5 are cut here to keep it short:

```bash
== STAGE 2/6: RAW FRAME (pre-COBS) =======================================
  off      bytes                    meaning
  0        01                       cmd  = CMD_CLOCK_INIT
  1        05                       node = node 5
  2        2A                       seq  = 42
  3-12     00 F8 FF FF 00 00    .. payload (10 B)
  13       BC                       xor  = 0xBC  (covers bytes 0..12)

  0000  01 05 2A 00 F8 FF FF 00  00 3C 5A 08 04 BC        |..*......<Z...|

  10 B payload + 3 B header + 1 B checksum = 14 B raw

== STAGE 3/6: COBS ENCODED ===============================================
  0000  04 01 05 2A 04 F8 FF FF  01 06 3C 5A 08 04 BC 00  |...*......<Z....|
        ^^          ^^           ^^ ^^                ^^

  14 B raw -> 16 B encoded (+2: 4 code bytes + 1 delimiter)
  ceiling for any frame: 295 B (MAX_ENCODED_FRAME), 279 B below
  cross-check vs buildFrame(): identical

== STAGE 4/6: ON THE WIRE ================================================
  16 B -> 0.16 ms @ 1000000 baud (8N1, 10 bits/byte)
  at 60 Hz: 1.0 kB/s -> 1.0 % of the ~100 kB/s ceiling

== STAGE 6/6: PARSED =====================================================
  result           OK
  command          CMD_CLOCK_INIT
  targetNode       0x05     node 5
  sequence         42
  payloadLen       10 bytes

  hand1Color       0xF800   r=255 g=0   b=0    red
  hand2Color       0xFFFF   r=255 g=255 b=255  white
  bgColor          0x0000   r=0   g=0   b=0    black
  hand1LengthPct   60 %
  hand2LengthPct   90 %
  hand1Thickness   8 px
  hand2Thickness   4 px

  round trip: payload matches the source byte for byte
```

</details>


<details>
<summary><strong>3.2 Physical testing</strong></summary>

### Objective

Put the protocol from 3.1 on a real RS485 bus. One master broadcasts, two slaves each pick their own slot out of the same broadcast and render it. Phase 3.1 only ever ran the protocol on the host through the frame inspector, so nothing had yet proven it works against real hardware, real timing and a real UART. 

`ClockFace.hpp` and `LGFX_Config.hpp` come from phase 1.1, `pin_definitions.h` from 2.1, and `protocol.h` is a byte-for-byte copy of the file the 3.1 unit tests cover. Only `src/main.cpp` on each side is new.

### Wiring

Three Picos on one bus, daisy chained A-to-A and B-to-B with a common ground. The master has no display. Both slaves have one wired exactly as in phase 1.1. Each Pico gets its own USB-C cable so all three can be watched on their own serial monitor. Per-board wiring is unchanged from phase 2.1 (RS485 module) and phase 1.1 (display):

| Board | RS485 module | Display | Direction pin (GP15) |
|---|---|---|---|
| Master | as in phase 2.1 | none | driven HIGH (permanent transmit) |
| Slave, node 0 | as in phase 2.1 | as in phase 1.1 | driven LOW (permanent receive) |
| Slave, node 36 | as in phase 2.1 | as in phase 1.1 | driven LOW (permanent receive) |

Only the master ever transmits, so the direction pins can stay fixed for the whole test and no turnaround timing is involved yet.

### Findings

**The slave needs both cores.** Drawing one frame takes about 11.6 ms (measured in phase 1.1), but a new `CLOCK_UPDATE` frame arrives roughly every 3 ms. So while one frame is being drawn, three or four more are already piling up on the wire. If reading and drawing happened in the same loop, the board simply wouldn't be listening to the bus for most of that 11.6 ms. The RP2040's UART buffer only holds 32 bytes by default, a tenth of one frame, so incoming bytes would get overwritten and lost. Worse, this would happen silently: the "frames/s" counter only counts what it manages to read, so it would keep reporting a clean number even while frames were being dropped.

The fix is to split the work across the RP2040's two CPU cores. Core 0 does nothing but read bytes off the bus and parse them. Core 1 does nothing but draw. That way, drawing never blocks reading. As extra insurance, the UART buffer is also enlarged to 1 KB (`Serial1.setFIFOSize()`), enough to hold about three whole frames, so even a brief stall on core 0, like a USB print taking a moment, can't cost a frame either.

**The two cores hand off data through a single slot.** Core 0 (reading) and core 1 (drawing) meet at one shared "mailbox" location, protected by a lock. Whenever core 0 has new data, it simply overwrites whatever is already sitting there, even if core 1 hasn't read it yet. This is on purpose: a queue would mean a node that falls behind has to work through a growing backlog of increasingly outdated angles. The hands would visibly lag. Overwriting instead means a slow node just skips straight to the latest position, which is what you actually want on a clock face. Every time this overwrite happens, it's counted as `superseded`. That count is how you tell "the bus lost a frame" apart from "this node just couldn't draw fast enough." Because each handoff only copies about 60 bytes and takes well under a microsecond, a simple lock is all that's needed here.

**Core 1 has to wait for core 0 before touching the display.** On this chip, core 1 actually starts running *before* core 0 reaches its own setup code. That's a problem, because core 0's setup is what speeds up the internal clock the display connection depends on. If core 1 initializes the display too early, it would end up running at a much slower default speed instead. The fix is a simple flag: core 1 just waits until core 0 signals "I'm ready" before touching the display. Similarly, all debug messages are printed from core 0 only. Both cores could technically print without literally corrupting each other's data (each print is protected by its own lock), but their output would still land on the screen interleaved and unreadable.

**The two slaves are nodes 0 and 36, not 0 and 1.** Each of the 72 nodes gets a starting angle 5 degrees apart from the next. If the two test boards were nodes 0 and 1, their hands would sit only 5 degrees apart, close enough that a wiring or addressing mistake (e.g. a board actually reading node 0's data instead of its own) could easily go unnoticed. Nodes 0 and 36 sit exactly opposite each other, so a mistake like that becomes obvious immediately: the two displays would move in perfect sync instead of independently.

**Re-sending the config regularly has to be free when nothing changed.** The master re-sends the `CLOCK_INIT` configuration every 5 seconds, so that a slave that reboots mid-test can catch up without needing the master restarted too. But re-applying that same configuration would normally mean rebuilding the hand graphics and redrawing the whole screen every 5 seconds, on every node, even when nothing actually changed. To avoid that, the slave compares each incoming configuration against the one it already has, and does nothing if they match. Pressing `i` on the master sends a genuinely different configuration, so you can watch the hands visibly change color as instant proof it worked. A new `invalidate()` function was added to `ClockFace` for the cases where a full redraw really is needed.

### How to test

Flash all three boards. The two slave environments differ only in the `MY_NODE_ID` baked in at build time:

```bash
pio run -d ./tests/phase-3.2-protocol-test-slave  -e slave0 -t upload
pio run -d ./tests/phase-3.2-protocol-test-slave  -e slave1 -t upload
pio run -d ./tests/phase-3.2-protocol-test-master -t upload
pio device monitor -d ./tests/phase-3.2-protocol-test-slave   # one terminal per slave
```

The master boots into `ALL` mode and rotates through every command type on its own, so it also works headless on a USB power brick. With a terminal attached, single keys steer it:

| Key | Effect |
|---|---|
| `a` | ALL — rotate CLOCK ↔ WIDGET with widget frames mixed in, exercises all four command types |
| `c` | CLOCK — `CLOCK_INIT` once, then `CLOCK_UPDATE` only, nothing else on the bus (the clean frame-rate run) |
| `w` | WIDGET — `WIDGET_INIT` once, then `WIDGET_UPDATE` at 2 Hz, alternating the two node addresses |
| `s` | STOP — the bus goes silent |
| `i` | send one `CLOCK_INIT` now, advancing to the next preset (hands change colour) |
| `I` | send one `WIDGET_INIT` now, next background colour |
| `u` | send one `WIDGET_UPDATE` now |
| `+` / `-` | step the `CLOCK_UPDATE` rate: 30 / 60 / 90 / 120 Hz |
| `?` | print the list |

What to check, in order:

1. **Boot** — each slave prints its node ID and waits. The master prints the payload and frame sizes.
2. **`c`** — both faces appear and turn smoothly, half a revolution out of phase. This is the addressing proof: two displays at the same angle would mean a slave is reading slot 0 instead of its own.
3. **Stats** — `rx` and `render` both around 60 f/s, `superseded` near zero, `fifo ok`, no bad frames and no sequence gaps.
4. **`i`** — the hands change colour and length instantly on both, with no ghosting from the previous face.
5. **`w`**, then `I` and `u` — both displays switch to the widget background and show their own field text, each naming the node it was addressed to. Then `c` returns to a clean clock face with no widget pixels left behind.
6. **`+` to 90 and 120 Hz** — `rx` should keep tracking the master while `render` plateaus near the ceiling measured in phase 1.1, with `superseded` climbing. That divergence is the point of the whole test: it separates a bus that cannot carry the traffic from a node that cannot draw it.
7. **`s`** — both slaves fall to 0 f/s and freeze, and `c` resumes cleanly.

The slave reports once a second:

```
[node 0] rx 60.1 f/s | render 59.8 f/s (avg 9.12ms max 9.94ms) | 0 bad | 0 gaps | 2 superseded | fifo ok
```

`rx` is what the protocol and the bus delivered, `render` is what this node actually drew. The two numbers answering differently is the useful result, not a fault.

### Results

```bash
TX 60.0 frames/s | 17.6 kB/s of ~100 kB/s | CLOCK @ 60 Hz | seq 149
[node 36] rx 60.0 f/s | render 60.0 f/s (avg 11.96ms max 12.30ms) | 0 bad | 0 gaps | 0 superseded | fifo ok
```

The steady-state clock run matches the master's 60 Hz TX rate, with render times consistent with the phase 1.1 baseline (~11.6 ms) and no bad frames, confirming the bus and node both handle the full 72-node broadcast at the design rate.

</details>
