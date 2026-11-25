# Software

This page holds the base principles that the master and the slave firmware both follow. It is a high-level description. The detailed architecture of each side is in [`master/README.md`](master/README.md) and [`slave/README.md`](slave/README.md).

## Contents

- [Layout](#layout)
- [Responsibilities](#responsibilities)
- [Code structure](#code-structure)
- [Concurrency](#concurrency)
- [Protocol compatibility](#protocol-compatibility)
- [Scope](#scope)
- [Known limitations](#known-limitations)

## Layout

| Path | Target | Content |
| --- | --- | --- |
| [`master/`](master/) | ESP32-S3 | PlatformIO project: animation engine, web UI, RS485 broadcast |
| [`slave/`](slave/) | RP2040 | PlatformIO project: RS485 receive, rendering of one display |
| [`shared/`](shared/) | both | The RS485 protocol, used by both projects |

Both projects use the Arduino framework (arduino-esp32 on the master, the earlephilhower arduino-pico core on the slave).

## Responsibilities

- **The master decides what is shown, the slave decides how to draw it.** A slave has no animation logic, no time and no knowledge of the grid. It renders the latest state it received for its own address. New animations therefore only need a master update, never a reflash of all nodes.
- **The master sets the pace.** It broadcasts at a fixed rate (target 60 Hz). A slave draws when new state arrives and has no timer of its own for content. This keeps all displays stepping at the same moment.
- **No new state, no change.** If packets stop arriving, a slave keeps showing the last state. There is no timeout or fallback screen.

## Code structure

- **`shared/` holds only the protocol.** It is the single source of truth for everything both sides must agree on: packet structs, constants, framing. It is plain C++ without Arduino or hardware includes, so it builds and is unit-tested natively on the PC.
- **Logic never touches hardware directly.** Each project has a core with the logic (e.g. animation engine, grid mapping, packet handling) and a thin layer for the hardware (UART, display, Wi-Fi, pins), with one interface per hardware piece. The core can then be unit-tested natively.

## Concurrency

Both the ESP32-S3 and the RP2040 have two cores. On both sides, one core talks to the outside world and the other does the timed work.

| | Core 0 | Core 1 |
| --- | --- | --- |
| Master | Wi-Fi, web UI, settings storage | Animation engine, frame building, RS485 broadcast |
| Slave | RS485 receive and parsing | Rendering |

- **Everything that has to happen every frame runs on the timed core.** On the master that is core 1, away from the Wi-Fi stack, which runs on core 0.
- **The timed core never waits for the other core.** Shared data is exchanged as a small snapshot behind a short lock, which the timed core copies once per frame.

## Protocol compatibility

The master will be flashed far more often than the 72 slaves, so the two sides can end up on different protocol versions.

- **The protocol version is bumped whenever something changes. The version is a simple variable inside protocol.h**
- **A slave that receives an unknown protocol version shows it visibly** (e.g. a red screen) instead of rendering garbage. 

## Scope

- **Clock and simple widgets.** For now the software covers the clock (digits and clock faces, with animated transitions) and simple widgets controlled from the master's web UI.

## Known limitations

- **Sync is not yet verified.** Whether all displays really step together has to be tested on the 2×2 prototype (phase 5.2b).
- **A frozen display can mean two things.** A crashed slave and a silent bus look the same, since both keep the last frame on screen.
