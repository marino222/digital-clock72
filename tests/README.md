# Tests

All tests carried out are described in this directory. The tests are split into the different phases explained in the [Roadmap](../README.md#roadmap). The respective subfolders contain dedicated test code, that also may be used in the final software.

| Folder | Description |
|---|---|
| [`phase-1.1-display-performance/`](phase-1.1-display-performance/) | Single GC9A01 dev board on a Pico, target 60 FPS |

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

**Onjective**
Test the RS485 connection by having a slave and a master talking to each other. The goal is to send a very barebone protocol via RS485 to a receiver. The receiver side utilizes the same test setup as in phase 1.1 and then uses the received package to display something. Both picos are connected and powered by separate USB-C cables. That way we can read both individual serial monitors. However, for RS485 to work reliabl they need to share a common ground.

**Wiring**

![GC9A01 Test](/docs/images/RS485_wiring_sender.PNG)
![GC9A01 Test](/docs/images/RS485_wiring_receiver.PNG)


</details>