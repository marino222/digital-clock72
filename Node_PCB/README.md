# Node_PCB

> **Status: In design.** The KiCad project for the single-node board lives here.
> (Formerly named `Smart_Tile_PCB/` under the superseded six-display "Smart Tile" concept.)

KiCad project for one **Pixel Node** board — the unit that is replicated **N times** (9 → 24
→ 72, scalable to ~250) to build the whole array. Designed for **full JLCPCB turnkey SMT
assembly — no hand soldering**.

## What's on the board

| Block | Part / detail |
|---|---|
| MCU | Bare **RP2040** (LCSC **C2040**, QFN-56) + 12 MHz crystal |
| Flash | External **2 MB QSPI** |
| Core rail | Internal 1.1 V switcher → 1 external inductor + caps |
| Display | **FPC connector** for a **GC9A01 1.28"** round LCD (240×240, SPI), driven SPI + DMA |
| Comms | **RS485** 1/8-unit-load transceiver (e.g. THVD1450); two paralleled bus connectors (in/out: 5V, GND, A, B) |
| Bus options | 120 Ω termination (end boards only) + fail-safe bias (master end only) — populate via jumper/DNP |
| Power | 3.3 V LDO (logic only) + raw 5 V (backlight); optional **mid-chain injection** input on the pilot |
| Flashing | **USB-C** (+ 2×5.1 kΩ CC pulldowns + BOOT button) and **3 SWD pads** |
| Protection | TVS across A/B (recommended) |

See the single-node block diagram in
[`docs/PCB_ARCHITECTURE.md`](../docs/PCB_ARCHITECTURE.md) §7.

## ⚠️ Before finalizing the layout

- **Confirm the GC9A01 FPC tail** (pin count / pitch / contacts-up vs -down) from *physical
  samples* — it defines the FPC footprint and is the single most common mistake on this board.
- **Measure real per-node power** first; it drives trace widths and connector ratings.
- Follow the Raspberry Pi minimal-RP2040 **reference layout** for the core (QFN-56 thermal
  pad + paste mask + via stitching) — not a place to improvise.

Full hardware rationale, BOM, cost, and assembly notes →
[`docs/PCB_ARCHITECTURE.md`](../docs/PCB_ARCHITECTURE.md).
Open hardware items → [`docs/OPEN_QUESTIONS.md`](../docs/OPEN_QUESTIONS.md).
