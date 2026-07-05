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
| VCC | 3V3 | 37 |
| GND | GND | 38|
| SCL | GP19 | 25 |
| SDA | GP18 | 24 |
| CS | GP20 | 26 |
| DC | GP17 | 22 |
| RST | GP16 | 21 |

**Procedure**
- Build/flash the PlatformIO project in `phase-1.1-display-performance/`
- Draw continuously to the display, measure achieved FPS (e.g. via Serial), sample for ~10s
- Pass criteria: sustains ~60 FPS

**Results**

| Date | Library | Avg FPS | Notes |
|---|---|---|---|
| — | — | — | — |

</details>
 