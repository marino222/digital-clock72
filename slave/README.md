# Slave_Firmware

> **Status: Conceptual.** Nothing implemented yet — this folder reserves the home for the
> node ("renderer") firmware.

Firmware for a single **Pixel Node** — one **RP2040** driving one **GC9A01** round LCD.

- **Tech:** C++ (**PlatformIO**), targeting the bare RP2040.
- **Renders locally:** holds a **full double framebuffer** and renders at **60 FPS** via
  **SPI + DMA** (CPU renders the next frame while the current one transfers).
- **Listens to the bus:** receives the master's 60 Hz global-state broadcast over **RS485**
  (UART RX ← RO; UART TX → DI; one GPIO → DE/RE direction).
- **Draws its slice:** reads its own **position/address** from the daisy chain and renders
  only the part of the global picture that belongs to it.
- **Auto-addressing:** position is assigned via **daisy-chain auto-addressing** — no DIP
  switches.

## To be designed/built here

- 60 FPS DMA + double-buffered render path (validated first in Phase 1a on a Pico).
- RS485 receive + the **broadcast protocol** decode (co-designed with `Master_Engine`).
- Daisy-chain **auto-addressing** logic.
- Per-mode renderers: clock first, then patterns and Snake.

## Bring-up reference (per board)

`power rails (3.3 V, 1.1 V) → BOOTSEL/USB enumeration → flash blink → display init → 60 FPS DMA render → RS485 loopback`

See the root [`README.md`](../README.md) (system architecture) and
[`Node_PCB/README.md`](../Node_PCB/README.md) (hardware this firmware runs on) for full context.
