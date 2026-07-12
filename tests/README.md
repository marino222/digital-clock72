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

**Procedure**
- Build/flash the PlatformIO project in `phase-1.1-display-performance/`
- Draw continuously to the display, measure achieved FPS (e.g. via Serial), sample for ~10s
- Pass criteria: sustains ~60 FPS

**Results**

### PlatformIO Config

- Must use the `earlephilhower` core, not official Arduino Mbed RP2040 core
- `Bus_SPI::config_t` on RP2040 does not have `dma_channel` / `use_lock` / `spi_3wire` (those are ESP32-only fields, won't compile). Working config only needs `spi_host`, `spi_mode`, `freq_write`, `freq_read`, `pin_sclk`, `pin_mosi`, `pin_miso`, `pin_dc`. DMA is handled internally by the platform, not user-toggled.

### SPI bus speed
- RP2040 hardware SPI clock is derived from the system clock (125MHz default) via an integer prescaler, practical ceiling is ~62.5MHz (sysclock ÷ 2).
Requesting a freq_write above that (e.g. 120MHz) is silently clamped by LovyanGFX to the nearest achievable divisor
- 40MHz (`SPI_WRITE_HZ`) is being used as the working starting point/"sweet spot" 
- `freq_read` set to 16MHz (lower than write — read path isn't performance-critical here since panel is write-only/readable = false).
- SPI clock alone is not the current bottleneck: at 40MHz, a full 240×240×16-bit frame (115,200 bytes) transfers in ≈23ms (~43 FPS ceiling), well above where the sprite/draw-side optimizations are currently landing

</details>
