# PCB_ARCHITECTURE.md — Digital Clock Array "Pixel Node"

## 0. Overview

This document specifies the hardware architecture for a single **Pixel Node**: one
self-contained board carrying one microcontroller and one round LCD, communicating
over a shared wired bus. The full installation is simply this identical board
replicated **N times** (target: 24 → 72 nodes, scalable to ~250 with no redesign).

The guiding principle is **one MCU per display**. Every architectural choice below
follows from that decision, and from the goal of a board that JLCPCB can assemble
**fully turnkey** (no hand soldering).

---

## 1. Topology — One MCU per Display

**Choice:** Each display gets its own dedicated microcontroller on its own small board.
This replaces the earlier "Smart Tile" concept (one Pico 2 driving six displays).

**Why:**

- **The framebuffer RAM math kills the multi-display approach.** One GC9A01 frame in
  RGB565 = 240 × 240 × 2 = **112.5 KiB**. Six displays single-buffered = **675 KiB**,
  which exceeds the RP2350's 520 KiB SRAM. The original "hold 6 full framebuffers"
  premise was never feasible; it would have forced fragile scanline/partial-buffer
  firmware. One display per MCU makes a full **double buffer (225 KiB)** trivially fit.
- **Firmware becomes trivial:** render one buffer, DMA it out over one SPI bus. No
  multi-display scheduling, SPI arbitration, or RAM contention.
- **True modular scalability:** the 4-display desk version and the 72-display array are
  *the same board* repeated. Scaling is a quantity change, not a redesign.
- **Field-replaceable:** a dead node is one cheap board swap, not a 6-display tile.
- **Cheaper, simpler boards:** a tiny single-MCU board is dirt-cheap to fab and route,
  versus a tile fanning out six length-matched high-speed SPI buses.

**Trade-off accepted:** higher MCU *count* (72 × RP2040 ≈ $50 vs 12 × RP2350 ≈ $13),
but the savings come from drastically simpler boards, easier assembly, and trivial
firmware. For a project paused by *assembly capacity*, this is the right trade.

---

## 2. Microcontroller — Bare RP2040 (QFN-56)

**Choice:** Bare RP2040 silicon placed directly on the node board (not a dev board,
not an ESP32 module).

**Why:**

- **No radio = no RF engineering.** The wired bus (Section 5) means WiFi is dead weight.
  Deleting the radio deletes the hardest parts of a custom board: impedance-controlled
  RF traces, a tuned matching network, antenna geometry, and **FCC/CE certification**.
  A bare ESP32 would force all of that onto us; the RP2040 is a plain digital chip.
- **Forgiving design:** plain 12 MHz crystal (CPU/USB reference only, not an RF
  reference), ordinary digital decoupling, and a published Raspberry Pi reference
  layout that is hand-solderable and clone-able.
- **Enough RAM:** 264 KiB SRAM double-buffers one display (225 KiB) with headroom;
  code runs from external flash (XIP), so RAM is almost all data.
- **Fully JLCPCB-assembleable:** stocked as LCSC **C2040**, QFN-56, SMT assembly type —
  pick-and-placed automatically.
- **Second-cheapest option** (only bare ESP32 silicon is cheaper, and that's the one we
  reject for RF/cert reasons).

**One added part:** RP2040 has no internal flash, so an external **QSPI flash** chip
(2 MB is plenty) is added. Trivial SPI device, no RF implications.

**Note:** RP2040 generates its 1.1 V core rail with an internal switcher → requires one
small external inductor + caps (well documented in the reference design).

---

## 3. Display — 1.28" GC9A01, FPC-Tail Version

**Choice:** 1.28" round GC9A01 LCD (240 × 240, RGB565, SPI), the **bare display with an
FPC ribbon tail** — *not* the breakout-board version with a 7-pin header.

**Why:**

- **No second PCB** behind each display; the breakout's carrier board and protruding
  pin row waste enclosure depth and add 24×/72× assembly tedium.
- The FPC tail plugs into an **FPC connector placed directly on the node board**. The
  display sits flat against the enclosure face. Assembly is "slide ribbon in, flip latch."

**Performance budget:** 240 × 240 × 16 bits = 900 kbit/frame. At ~62.5 MHz SPI that's
~14.4 ms transfer; with **DMA + double buffering** the CPU renders the next frame during
the transfer, making **60 FPS comfortably achievable** for a single display.

**⚠️ Must verify before finalizing PCB:** GC9A01 FPC tails vary by supplier in **pin
count, pitch, and contact orientation (contacts-up vs contacts-down)**. The on-board FPC
connector must match all three. **Order 1–2 sample displays first** and confirm the tail
matches the datasheet before committing the layout — cheap displays sometimes ship a
different tail than advertised. This is the single most common mistake on this board type.

---

## 4. Firmware Flashing — USB-C + BOOTSEL, with SWD Pads

**Choice:** A **USB-C** connector and **BOOT button** on each node for convenient
PlatformIO flashing, plus **3 SWD pads** for recovery and hardware debug.

**Why USB-C:** matches the intended PlatformIO drag-and-drop / picotool workflow. After
the first manual BOOTSEL flash, the Arduino-Pico core auto-resets into the bootloader on
each upload (one click) as long as the running firmware enumerates USB serial.

**Required for a USB-C port that actually enumerates:**

- **2 × 5.1 kΩ CC pulldown resistors** (CC1 and CC2 to GND). *The #1 cause of a dead
  custom USB-C port is omitting these.*
- **BOOT button** (or two shorting pads): a blank or crashed RP2040 won't present for
  flashing on its own; BOOTSEL is needed for the first flash and for un-bricking.
- **D+/D− as a short ~90 Ω differential pair**, kept short and roughly length-matched.

**Why SWD pads too (near-free, recommended):** 3 bare pads (SWCLK, SWDIO, GND) cost
essentially nothing and give a **recovery path** when USB enumeration breaks (so a bad
firmware push never permanently bricks a board) and enable **real hardware debugging**
in PlatformIO (breakpoints, stepping) — which UF2-over-USB cannot do.

**Assembly note:** USB-C connector, CC resistors, and BOOT button are all in JLC's
library and pick-and-placed automatically — none of this forces hand soldering.

---

## 5. Communication — RS485 Wired Bus

**Choice:** Half-duplex **RS485** multidrop bus, daisy-chained node to node. The master
(Pi 3) broadcasts tiny state packets at 60 Hz; each node renders pixels locally.

**Why wired RS485 over WiFi:**

- **Determinism:** frame-locked 60 Hz sync needs predictable, low-jitter timing. WiFi's
  tens-of-ms jitter, retries, and channel contention are poison for sync.
- **Native broadcast:** one differential transmit reaches all nodes at once — exactly the
  state-broadcast model. (WiFi multicast is unreliable/rate-limited.)
- **Robust & cheap:** a multidrop differential pair beats N radios contending for
  2.4 GHz, and 5 V + GND already runs to every node, so the comms pair is nearly free.

**Per-node RS485 components:**

| Item | Detail |
|---|---|
| Transceiver IC | **1/8-Unit-Load, 3.3 V part** — e.g. **THVD1450** (1/8 UL → up to 256 nodes), SN65HVD75. 3.3 V-native avoids level shifting. |
| MCU connections | UART TX → DI, UART RX → RO, **one GPIO → DE/RE** (direction). Slaves mostly listen; DE/RE on a GPIO keeps reply capability optional. |
| Bus connectors | **Two identical connectors (in + out), pins paralleled** so the bus passes through each board — clean daisy chain. Min 4 pins: 5V, GND, A, B. |
| Termination | **120 Ω across A/B, only at the two physical end boards.** Populate the footprint on every board but enable via solder-jumper/DNP only at the ends. |
| Fail-safe bias | Pull A high / B low (~560 Ω–1 kΩ), **once on the bus** (master end), to define a clean idle level. |
| Protection (recommended) | **TVS diode** across A/B for ESD/surge, important on a long 72-node chain. |

**Why the 1/8-UL transceiver is chosen up front:** standard transceivers are 1 UL and the
spec allows only **32 UL per segment** → 72 standard nodes overruns the bus. A 1/8-UL
part costs the same and lets the **exact same board scale from 24 → 72 → ~250** with no
respin. This is the key future-proofing decision.

**Layout rule:** keep the A/B stub from the bus connectors to the transceiver **short**.
Run the bus only as fast as needed — tiny state packets don't need megabaud, and lower
baud buys far more distance/noise tolerance.

---

## 6. Power

> Power is the **least-known quantity** in the design and the one to *measure early*
> (see Phase 1a, Section 10). Numbers below are placeholders until measured.

### 6.1 Local (on-PCB) regulation — two separate rails

Each node must split power into two paths. Getting this split wrong is a common mistake:

- **3.3 V LDO → logic only** (RP2040, QSPI flash, display logic). Size for ~100–150 mA;
  stays small and cool. An LDO dissipates (5 − 3.3) V × I as heat — fine at logic
  currents, which is exactly *why the backlight must not go through it*.
- **5 V (raw rail) → display backlight.** The LED string wants ~5 V and is the biggest
  current draw. Run BL from the raw 5 V rail directly, or via a transistor/PWM stage off
  5 V for dimming. **Never route the backlight through the 3.3 V LDO** — it would overload
  and cook it.
- **RP2040 core:** internal 1.1 V switcher → one external inductor + caps.

### 6.2 Bus power distribution — the part that scales badly

5 V + GND are carried node-to-node alongside A/B on the daisy chain. Two effects grow
with node count and **must be watched even on the 3×3 pilot**:

- **Voltage droop:** each node pulls current *through* the previous node's connectors and
  traces. The last node sees less than 5 V (dimmer backlight, brownout risk). Measurable
  directly: 5 V at node 1 vs. the last node under full white.
- **Connector/trace current:** current near the feed-in end is the *sum* of all downstream
  nodes. 9 nodes × ~250 mA ≈ 2.25 A through the first connector; 72 nodes ≈ 18 A — a
  single feed is impossible at scale.

**Mitigation = power injection:** feed 5 V/GND at multiple points along the chain rather
than from one end. Build a **mid-chain injection point** into the pilot board (a second
5 V/GND input) to test how it flattens droop — that experiment yields the rule for the
full array.

### 6.3 System power budget (placeholder — replace with measured per-node value)

| Nodes | Typical @ 5 V | Peak @ 5 V (all white) | PSU approach |
|---|---|---|---|
| 9 (pilot) | ~1.4 A | ~2.25 A | Single 5 V / 5 A (headroom for inrush) |
| 24 | ~3.8 A (19 W) | ~6.0 A (30 W) | 5 V 8 A (40 W) |
| 72 | ~11.5 A (58 W) | ~18 A (90 W) | **Multiple supplies + injection points** |

> Also budget for **inrush** when many backlights light at once on power-up.

---

## 7. Single-Node Block Summary

```
        ┌──────────────────────────────────────────────┐
        │                 PIXEL NODE                    │
        │                                               │
  USB-C ─┤ USB-C + 2×5.1k CC + BOOT btn ──┐             │
  SWD  ──┤ SWCLK/SWDIO/GND pads           │             │
        │                          ┌──────┴──────┐      │
        │   12MHz xtal ──────────► │   RP2040    │      │
        │   QSPI flash (2MB) ◄────►│  (QFN-56)   │      │
        │   1.1V switcher (L+C) ──►│             │      │
        │                          └──┬───┬───┬──┘      │
        │            SPI+DMA ─────────┘   │   └──── UART/DE,RE
        │               │                 │            │      │
        │         ┌─────▼─────┐    ┌──────▼──────┐ ┌───▼────┐ │
        │         │ FPC conn  │    │  3.3V LDO   │ │ RS485  │ │
        │         │ → GC9A01  │    │ (from 5V)   │ │ 1/8 UL │ │
        │         └───────────┘    └─────────────┘ └─┬────┬─┘ │
        │                                            │    │   │
        └────────────────────────────── BUS IN ◄─────┘    └──► BUS OUT
                                       (5V,GND,A,B)      (5V,GND,A,B)
```

---

## 8. Rough Cost Estimate

> ⚠️ **Estimates, not a quote.** JLCPCB fab/assembly fees and part prices shift over time.
> Pull a live quote with your finalized BOM before committing. Display price dominates and
> varies most by supplier.

### Per-node Bill of Materials (electronics)

| Item | Est. unit cost |
|---|---|
| RP2040 (C2040) | $0.70 |
| QSPI flash, 2 MB | $0.12 |
| 12 MHz crystal | $0.12 |
| 3.3 V LDO | $0.12 |
| Core switcher inductor | $0.04 |
| USB-C connector | $0.12 |
| BOOT button | $0.03 |
| RS485 transceiver (THVD1450) | $0.50 |
| 2 × bus connectors | $0.20 |
| FPC connector | $0.15 |
| Passives (CC res, decoupling, bias, term, ~25 parts) | $0.25 |
| **MCU/board electronics subtotal** | **≈ $2.35** |
| GC9A01 1.28" FPC display | $3.00 – $4.00 |
| Bare PCB (2-layer, at volume) | $0.30 – $0.50 |
| SMT assembly (per-board share, see below) | $0.20 – $0.40 |
| **Estimated total per node** | **≈ $6.00 – $7.50** |

### One-time / shared (per order)

| Item | Est. |
|---|---|
| SMT setup fee | ~$8 |
| Stencil | ~$1.50 |
| Extended-part feeders (RP2040, flash, crystal, transceiver, FPC, USB-C…) | ~$3 each |
| RP2040 assembly fixture surcharge | a few $ |

### System totals (electronics only, excl. PSU/enclosure/wiring)

| Nodes | Per-node × N | + one-time | **Rough total** |
|---|---|---|---|
| 24 | ~$160 | ~$40 | **≈ $200** |
| 72 | ~$475 | ~$45 | **≈ $520** |

Not included: 5 V PSU(s), 3D-printed enclosures, inter-node cabling/connectorized
harness, the Raspberry Pi 3 master, and a ~$12 Raspberry Pi Debug Probe (one tool flashes
the whole fleet via SWD if needed).

---

## 9. Assembly Method

**Full JLCPCB turnkey SMT assembly — no hand soldering.**

- **Every component is pick-and-placeable** and in JLC's library: the RP2040 (QFN-56,
  C2040), QSPI flash, crystal, RS485 transceiver, USB-C connector, FPC connector, BOOT
  button, and all passives. This was a deciding factor in choosing the bare RP2040 over a
  cheap AliExpress module (modules are *not* in LCSC and would force hand soldering).
- **Production files** (Gerber + BOM + Pick-and-Place) exported from KiCad/EasyEDA; a
  JLCPCB BOM helper plugin maps each part to its LCSC number.
- **QFN-56 care:** the RP2040 thermal pad and fine pitch are prone to solder bridging.
  Follow the reference footprint, paste mask, and thermal-pad via stitching exactly —
  this is not a place to improvise. JLC notes the part needs an **assembly fixture**
  (small one-time surcharge). Inspect RP2040 and USB-C joints first on the prototype.
- **Selective population** lets one board design serve all roles:
  - Termination (120 Ω) populated/enabled only on the two **end** boards.
  - Fail-safe bias populated only on the **master-end** board.
  - Optionally leave USB-C unpopulated on fleet boards if cost/space matters later
    (SWD pads still allow flashing) — though current plan is USB-C on all nodes.

**Recommended bring-up order:**

1. Order **1–2 sample GC9A01 FPC displays**; confirm tail pin count/pitch/orientation.
2. Finalize node layout cloning the Raspberry Pi minimal-RP2040 reference for the core.
3. Order a **small first batch (e.g. 5 boards)** fully assembled by JLC.
4. Bring up one board: power rails (3.3 V, 1.1 V) → BOOTSEL/USB enumeration → flash blink
   → display init → 60 FPS DMA render test → RS485 loopback.
5. Validate a **2-node RS485 link**, then a short chain, before committing to 24 or 72.

---

## 10. Implementation & Bring-up Plan

**Philosophy:** prove the riskiest cheap thing first; spend money on PCBs only after the
unknowns are gone. The design is intentionally scalable, so the pilot is a **3×3 (9-node)**
array — large enough to exercise the multi-node bus, daisy-chain addressing, sync, and
power sharing, but cheap and debuggable. Each phase has a clear pass/fail gate.

### Phase 1a — Display performance (breadboard)
- One GC9A01 **dev board** driven by a **Pico**.
- **Gate:** hit the real target — **60 FPS, DMA, double-buffered**. This validates the
  performance budget the whole architecture rests on. If 60 FPS isn't comfortable, find
  out here, not after 10 PCBs.
- **Also measure power here** (see Phase below) — this is the cheapest place to get it.

### Phase 1b — RS485 link (breadboard)
- Two **Picos** + two cheap RS485 breakout modules (MAX3485 / THVD1450).
- **Gate:** master broadcasts state packets, slave renders locally; packet format and the
  60 Hz broadcast loop proven before anything is on a PCB.

### Phase 1-power — Measurement protocol (run during 1a)
Put a USB power meter or bench supply (with current readout) inline and record:
- **Idle / typical content** (clock face, sparse pixels)
- **Full white** (worst case: backlight + all pixels)
- **Inrush** at power-on (spike when displays light)

→ Replace all placeholder power numbers (Section 6) with **measured per-node value × N**.
This turns PSU sizing, connector ratings, and trace widths from guesses into facts.

### Phase 2 — FPC display samples
- Order **~10 GC9A01 FPC-tail displays** *before finalizing any PCB*.
- **Gate:** physically confirm tail **pin count / pitch / contact orientation** against
  the datasheet. The FPC connector footprint is the one thing on the board that can't be
  cloned from a reference — it's defined by the display that actually arrives.

### Phase 3 — Single-node board (the key insurance step)
- Order **~5 of a single-node board** and fully bring up **one**.
- **Why:** the QFN-56 RP2040 core + FPC connector are the only genuinely new/risky parts
  to solder. Prove one board powers up, enumerates, flashes, and drives a display before
  multiplying by 10. A footprint bug is then a ~$30 lesson, not a ~$150 one.
- **Bring-up order:** power rails (3.3 V, 1.1 V) → BOOTSEL/USB enumeration → flash blink
  → display init → 60 FPS DMA render → RS485 loopback.

### Phase 4 — 3×3 pilot (system test)
- Order **~10 boards** (9 nodes + spare). This is now a *system* test, not a board test.
- **Test:** daisy-chain bus, auto-addressing, 9-node sync, and **power as a deliberate
  experiment** — measure 5 V droop node 1 → node 9 under full white, and test the
  **mid-chain injection point** to see how it flattens droop.
- **Also prove the mechanical chain here:** the real inter-board connector, cable, strain
  relief, and polarization (so a board can't be plugged in backwards). Don't defer the
  harness to "later" — it's a real source of pain. Pick the production connector now.
- **Pilot PSU:** ~9 × measured peak with margin (≈ 5 V / 5 A if peak ≈ 250 mA/node).

### Phase 5 — Scale
- With measured power numbers, a proven board, and a validated bus, scale to 24 → 72.
- Apply the injection-point rule learned in Phase 4; treat 72-node power distribution as
  its own design pass (multiple supplies, injection points, trace/connector currents).

**Refined sequence at a glance:**
`1a display perf → 1b RS485 link (+ measure power) → 2 FPC samples → 3 single-node board → 4 3×3 pilot → 5 scale`

---

## 11. Decision Log (Quick Reference)

| Decision | Choice | Primary reason |
|---|---|---|
| Topology | 1 MCU per display | Framebuffer RAM; simple firmware; modular scaling |
| MCU | Bare RP2040 (C2040) | No radio → no RF/cert; cheap; JLC-placeable; enough RAM |
| Flash | External 2 MB QSPI | RP2040 has none |
| Display | GC9A01 1.28", **FPC tail** | No 2nd PCB; flat mount; clean assembly |
| Frame rate | 60 FPS, DMA + double buffer | Free per-display once RAM pressure is gone |
| Flashing | USB-C + BOOT (+ SWD pads) | PlatformIO workflow; SWD for recovery/debug |
| Comms | RS485, **1/8-UL transceiver** | Deterministic, broadcast, robust, scales to 256 |
| Bus wiring | 2 connectors, daisy chain | Pass-through bus; identical boards |
| Termination/bias | Optional-populate footprints | One board serves end + middle roles |
| Addressing | Daisy-chain auto-address (firmware) | No DIP switches; scales to any count |
| Assembly | Full JLCPCB turnkey SMT | All parts in LCSC; no hand soldering |

---

## 12. Open Items Before Production

- [ ] Confirm GC9A01 FPC tail spec (pin count / pitch / orientation) from physical samples.
- [ ] **Measure real per-node power** (idle / full white / inrush) in Phase 1a; replace placeholders.
- [ ] Verify local rail split: backlight on raw 5 V, logic-only through the 3.3 V LDO.
- [ ] Choose exact transceiver P/N and confirm LCSC stock (THVD1450 vs SN65HVD75).
- [ ] Finalize bus connector (pin count, current rating, polarization, strain relief).
- [ ] Power-distribution plan for 72 nodes (injection points, trace/connector current).
- [ ] Pull a live JLCPCB quote with the finalized BOM.
- [ ] Decide baud rate vs. max bus length for the largest planned installation.
