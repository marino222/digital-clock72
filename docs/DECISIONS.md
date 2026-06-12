# DECISIONS.md — Consolidated Decision Log

This file gathers the **key design decisions** for the Digital Kinetic Clock Array into one
place, with the primary reason for each. Decisions are split across the two canonical
documents — [`PROJECT_OVERVIEW.md`](PROJECT_OVERVIEW.md) (system level) and
[`PCB_ARCHITECTURE.md`](PCB_ARCHITECTURE.md) (node hardware) — and consolidated here for
quick reference. The cited sections point to the full rationale.

---

## System-level decisions

| Decision | Choice | Primary reason | Detail |
|---|---|---|---|
| Overall architecture | Hybrid **"brain + many renderers"** (master/slave) | Real-time pixel timing and global C#/web/game logic each belong on different hardware | OVERVIEW §3 |
| Master ("brain") platform | **Raspberry Pi 3**, headless Linux, C# / .NET 8, Dockerized | Heavy global logic + web UI is awkward on an MCU, comfortable on a Pi | OVERVIEW §3.1 |
| Master resilience | **Read-only filesystem (OverlayFS)** | Random power cuts must not corrupt the SD card — appliance behaviour | OVERVIEW §3.1 |
| Coordination model | **State broadcast, not pixel streaming** | Tiny packets vs. huge frame streams; sync lives at the state layer; scales cleanly from 9 → 72 | OVERVIEW §4 |
| Broadcast rate | **60 Hz** global-state packets | Frame-locked smooth animation across the array | OVERVIEW §4 |
| Applications | Time, geometric/animated patterns, array-wide games (Snake) | All are just different *global states* the master broadcasts | OVERVIEW §5 |
| Target scale | **3×3 (9) pilot → 24 → 72** | "One identical node, replicated" — scale is a quantity decision, not a redesign | OVERVIEW §1 |

---

## Node hardware decisions

| Decision | Choice | Primary reason | Detail |
|---|---|---|---|
| Topology | **1 MCU per display** | Framebuffer RAM math kills multi-display; trivial firmware; modular scaling; field-replaceable | PCB §1 |
| Microcontroller | **Bare RP2040** (LCSC **C2040**, QFN-56) | No radio → no RF traces / matching / antenna / FCC-CE cert; cheap; JLC pick-and-placeable; enough RAM | PCB §2 |
| Flash | **External 2 MB QSPI** | RP2040 has no internal flash | PCB §2 |
| Core rail | Internal 1.1 V switcher (1 external inductor + caps) | Per the RP2040 reference design | PCB §2, §6.1 |
| Display | **GC9A01 1.28" round, FPC-tail version** (240×240, RGB565, SPI) | No second PCB behind each display; flat mount; clean "slide ribbon in, flip latch" assembly | PCB §3 |
| Frame rate | **60 FPS, DMA + double buffer** | Effectively free per-display once one-MCU-per-display removes RAM pressure | PCB §3 |
| Flashing | **USB-C + BOOT button**, plus **3 SWD pads** | Matches PlatformIO drag-and-drop workflow; SWD gives a recovery/debug path that UF2-over-USB can't | PCB §4 |
| USB-C correctness | 2 × 5.1 kΩ CC pulldowns; short ~90 Ω D+/D− pair | #1 cause of a dead custom USB-C port is omitting the CC resistors | PCB §4 |
| Comms transport | **Half-duplex RS485**, multidrop, daisy-chained | Deterministic low-jitter timing, native broadcast, noise-immune — WiFi jitter/contention is unacceptable for frame-locked sync | PCB §5, OVERVIEW §4 |
| RS485 transceiver | **1/8-unit-load, 3.3 V part** (e.g. THVD1450; alt SN65HVD75) | 1 UL parts cap a segment at 32 nodes; 1/8 UL scales the *same board* to ~250 with no respin; 3.3 V-native avoids level shifting | PCB §5 |
| Bus wiring | **Two paralleled connectors (in + out)** per board, min 4 pins (5V, GND, A, B) | Pass-through bus → clean daisy chain from identical boards | PCB §5 |
| Termination | **120 Ω across A/B, end boards only** (footprint on all, enabled via jumper/DNP at the two ends) | One board design serves both end and middle roles | PCB §5, §9 |
| Fail-safe bias | Pull A high / B low (~560 Ω–1 kΩ), **once** at the master end | Defines a clean idle level on the bus | PCB §5 |
| Bus protection | **TVS diode across A/B** (recommended) | ESD/surge protection on a long 72-node chain | PCB §5 |
| Addressing | **Daisy-chain auto-addressing in firmware** | No DIP switches; scales to any node count | PCB §11, OVERVIEW §9 |
| Power rail split | **3.3 V LDO → logic only; backlight on raw 5 V** | An LDO would cook trying to carry the backlight current; logic currents are fine | PCB §6.1 |
| Power distribution | **Multi-point injection** (build a mid-chain injection point into the pilot) | Single-end feed droops/overheats: ~2.25 A through the first connector at 9 nodes, ~18 A at 72 | PCB §6.2 |
| Assembly | **Full JLCPCB turnkey SMT — no hand soldering** | Every part is in LCSC and pick-and-placeable; deciding factor in choosing bare RP2040 over a non-LCSC module | PCB §9 |

---

## Notable superseded decision

- **"Smart Tile" (one MCU driving six displays) → superseded by one-MCU-per-display.**
  One GC9A01 frame in RGB565 is 112.5 KiB; six single-buffered = 675 KiB, exceeding the
  RP2350's 520 KiB SRAM. The "hold 6 framebuffers" premise was never feasible and would have
  forced fragile scanline/partial-buffer firmware. One display per MCU makes a full double
  buffer (225 KiB) trivially fit. Trade-off accepted: higher MCU *count*
  (72 × RP2040 ≈ $50 vs 12 × RP2350 ≈ $13) in exchange for drastically simpler boards, easier
  assembly, and trivial firmware — the right trade for a project bottlenecked on assembly
  capacity. (PCB §1, OVERVIEW §6.)

---

*Section keys: **OVERVIEW** = [`PROJECT_OVERVIEW.md`](PROJECT_OVERVIEW.md), **PCB** =
[`PCB_ARCHITECTURE.md`](PCB_ARCHITECTURE.md). See those files for full rationale.*
