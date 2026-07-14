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

**How to test**
- Build/flash the PlatformIO project in `phase-1.1-display-performance/`
- Draw continuously to the display, measure achieved FPS (e.g. via Serial), sample for ~10s
- Pass criteria: sustains ~60 FPS

</details>
