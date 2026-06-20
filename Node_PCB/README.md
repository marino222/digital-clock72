# Node_PCB

> **Status: In design.** The KiCad project for the single-node board lives here.
> (Formerly named `Smart_Tile_PCB/` under the superseded six-display "Smart Tile" concept.)

KiCad project — and full hardware architecture write-up — for one **Pixel Node** board: a
single self-contained board carrying one microcontroller and one round LCD, communicating
over a shared wired bus. The full installation is simply this identical board replicated
**N times** (target: 24 → 72 nodes, scalable to ~250 with no redesign), designed for
**full JLCPCB turnkey SMT assembly — no hand soldering**.

For the system-level picture (why a master/slave split, what the master does, what this
node fits into) see the root [`README.md`](../README.md). The hardware bring-up plan and
test phases also live there, in the [Roadmap](../README.md#roadmap) — this README covers
the board design only.

---

## Table of contents

- [What's on the board](#whats-on-the-board)
- [Key decisions](#key-decisions)
- [Hardware deep dive](#hardware-deep-dive)
- [Power](#power)
- [Block diagram](#block-diagram)
- [Cost estimate](#cost-estimate)
- [Assembly](#assembly)
- [Decision log](#decision-log)
- [Open items before production](#open-items-before-production)

---

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

### Before finalizing the layout

- **Confirm the GC9A01 FPC tail** (pin count / pitch / contacts-up vs -down) from *physical
  samples* — it defines the FPC footprint and is the single most common mistake on this board.
- **Measure real per-node power** first; it drives trace widths and connector ratings.
- Follow the Raspberry Pi minimal-RP2040 **reference layout** for the core (QFN-56 thermal
  pad + paste mask + via stitching) — not a place to improvise.

---

## Key decisions

| Aspect | Choice | Why |
|---|---|---|
| Topology | One MCU per display | A 240×240 RGB565 frame is 112.5 KiB; six displays sharing one MCU (675 KiB) would exceed even an RP2350's 520 KiB SRAM and force fragile partial-buffer firmware. One MCU per display fits a full double buffer (225 KiB) easily. |
| MCU | Bare RP2040 (LCSC C2040, QFN-56) | The wired bus means no radio is needed — deleting RF engineering, antenna design, and FCC/CE certification. 264 KiB SRAM comfortably double-buffers one display, and the part is fully JLCPCB-assembleable. |
| Display | GC9A01 1.28", FPC-tail version | The bare FPC version skips a second carrier PCB and protruding pin header, so the display sits flat against the enclosure face. |
| Flashing | USB-C + BOOT button + SWD pads | Matches the intended PlatformIO drag-and-drop workflow; the near-free SWD pads add a recovery path if USB enumeration ever breaks and enable real hardware debugging. |
| Comms | RS485, 1/8-unit-load transceiver (e.g. THVD1450) | Deterministic and natively broadcast over a long daisy chain. A 1/8-UL part lets the *same* board scale from 24 → 72 → ~250 nodes with no respin — a standard 1-UL transceiver caps out around 32 nodes per segment. |
| Power rail split | 3.3 V LDO for logic only; backlight on raw 5 V | The backlight is the dominant current draw; routing it through the 3.3 V LDO would make the regulator dissipate (5 − 3.3) V × I as heat and cook it. |

**Trade-off accepted:** one MCU per display means a higher MCU *count* (72 × RP2040 ≈ $50
vs. 12 × RP2350 ≈ $13 for a six-display "tile" board), but the savings come from drastically
simpler boards, easier assembly, and trivial firmware — the right trade for a project
bottlenecked on assembly capacity rather than part cost.

---

## Hardware deep dive

### One MCU per display

Beyond the framebuffer math above, splitting one MCU per display keeps firmware trivial:
render one buffer, DMA it out over one SPI bus — no multi-display scheduling, SPI
arbitration, or RAM contention. It also keeps the system modular: the same board serves a
4-display desk version and the 72-display array, scaling is a quantity change rather than a
redesign, and a dead node is a single cheap board swap rather than a multi-display tile.

### Bare RP2040

A plain 12 MHz crystal (CPU/USB reference only, not an RF reference), ordinary digital
decoupling, and the published Raspberry Pi reference layout make this a forgiving, hand-
clonable design. Since the RP2040 has no internal flash, an external 2 MB QSPI chip is
added — a trivial SPI device with no RF implications. The RP2040 also generates its 1.1 V
core rail from an internal switcher, which needs one small external inductor plus caps
(documented in the reference design).

### Display performance and FPC risk

240 × 240 × 16 bits = 900 kbit/frame. At ~62.5 MHz SPI that's a ~14.4 ms transfer; with DMA
and double buffering the CPU renders the next frame during the transfer, making 60 FPS
comfortably achievable for a single display. The one real risk is the FPC tail itself: pin
count, pitch, and contact orientation (contacts-up vs. -down) vary by supplier, and the
on-board FPC connector footprint can't be cloned from a reference — it's defined entirely by
the display that actually arrives (see [Before finalizing the layout](#before-finalizing-the-layout)).

### Flashing and debug access

A USB-C port that actually enumerates needs **2 × 5.1 kΩ CC pulldown resistors** (CC1/CC2 to
GND — omitting these is the #1 cause of a dead custom USB-C port) and a short, roughly
length-matched ~90 Ω D+/D− differential pair. A **BOOT button** (or shorting pads) is
required because a blank or crashed RP2040 won't present for flashing on its own. The 3 bare
SWD pads (SWCLK, SWDIO, GND) cost essentially nothing and give a recovery path when USB
enumeration breaks, plus real hardware debugging (breakpoints, stepping) that UF2-over-USB
can't offer. All of these — USB-C connector, CC resistors, BOOT button — are in JLC's
library and pick-and-placed automatically.

### RS485 bus details

| Item | Detail |
|---|---|
| Transceiver IC | **1/8-Unit-Load, 3.3 V part** — e.g. **THVD1450** (1/8 UL → up to 256 nodes), SN65HVD75. 3.3 V-native avoids level shifting. |
| MCU connections | UART TX → DI, UART RX → RO, **one GPIO → DE/RE** (direction). Slaves mostly listen; DE/RE on a GPIO keeps reply capability optional. |
| Bus connectors | **Two identical connectors (in + out), pins paralleled** so the bus passes through each board — clean daisy chain. Min 4 pins: 5V, GND, A, B. |
| Termination | **120 Ω across A/B, only at the two physical end boards.** Populate the footprint on every board but enable via solder-jumper/DNP only at the ends. |
| Fail-safe bias | Pull A high / B low (~560 Ω–1 kΩ), **once on the bus** (master end), to define a clean idle level. |
| Protection (recommended) | **TVS diode** across A/B for ESD/surge, important on a long 72-node chain. |

**Layout rule:** keep the A/B stub from the bus connectors to the transceiver **short**. Run
the bus only as fast as needed — tiny state packets don't need megabaud, and lower baud buys
far more distance/noise tolerance.

---

## Power

> Power is the **least-known quantity** in the design and the one to *measure early* (see
> the main README's [Phase 1a](../README.md#phase-1a--display-performance-breadboard)).
> Numbers below are placeholders until measured.

### Local regulation — two separate rails

Each node must split power into two paths. Getting this split wrong is a common mistake:

- **3.3 V LDO → logic only** (RP2040, QSPI flash, display logic). Size for ~100–150 mA;
  stays small and cool. An LDO dissipates (5 − 3.3) V × I as heat — fine at logic
  currents, which is exactly *why the backlight must not go through it*.
- **5 V (raw rail) → display backlight.** The LED string wants ~5 V and is the biggest
  current draw. Run BL from the raw 5 V rail directly, or via a transistor/PWM stage off
  5 V for dimming. **Never route the backlight through the 3.3 V LDO** — it would overload
  and cook it.
- **RP2040 core:** internal 1.1 V switcher → one external inductor + caps.

### Bus power distribution — the part that scales badly

5 V + GND are carried node-to-node alongside A/B on the daisy chain. Two effects grow with
node count and **must be watched even on the 3×3 pilot**:

- **Voltage droop:** each node pulls current *through* the previous node's connectors and
  traces. The last node sees less than 5 V (dimmer backlight, brownout risk). Measurable
  directly: 5 V at node 1 vs. the last node under full white.
- **Connector/trace current:** current near the feed-in end is the *sum* of all downstream
  nodes. 9 nodes × ~250 mA ≈ 2.25 A through the first connector; 72 nodes ≈ 18 A — a single
  feed is impossible at scale.

**Mitigation = power injection:** feed 5 V/GND at multiple points along the chain rather
than from one end. Build a **mid-chain injection point** into the pilot board (a second
5 V/GND input) to test how it flattens droop — that experiment yields the rule for the full
array.

### System power budget (placeholder — replace with measured per-node value)

| Nodes | Typical @ 5 V | Peak @ 5 V (all white) | PSU approach |
|---|---|---|---|
| 9 (pilot) | ~1.4 A | ~2.25 A | Single 5 V / 5 A (headroom for inrush) |
| 24 | ~3.8 A (19 W) | ~6.0 A (30 W) | 5 V 8 A (40 W) |
| 72 | ~11.5 A (58 W) | ~18 A (90 W) | **Multiple supplies + injection points** |

> Also budget for **inrush** when many backlights light at once on power-up.

---

## Block diagram

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

## Cost estimate

> ⚠️ **Estimates, not a quote.** JLCPCB fab/assembly fees and part prices shift over time.
> Pull a live quote with your finalized BOM before committing. Display price dominates and
> varies most by supplier.

### Per-node bill of materials (electronics)

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

Not included: 5 V PSU(s), 3D-printed enclosures, inter-node cabling/connectorized harness,
the Raspberry Pi 3 master, and a ~$12 Raspberry Pi Debug Probe (one tool flashes the whole
fleet via SWD if needed).

---

## Assembly

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

The phase-by-phase bring-up order (sample displays → layout → first batch → single-board
bring-up → multi-node validation) is detailed in the main README's
[Roadmap](../README.md#roadmap).

---

## Decision log

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
| Power rail split | 3.3 V LDO logic-only; backlight on raw 5 V | LDO would cook trying to carry backlight current |
| Power distribution | Multi-point injection | Single-end feed droops/overheats at scale |
| Addressing | Daisy-chain auto-address (firmware) | No DIP switches; scales to any count |
| Assembly | Full JLCPCB turnkey SMT | All parts in LCSC; no hand soldering |

**Notable superseded decision:** "Smart Tile" (one MCU driving six displays) → one-MCU-
per-display. One GC9A01 frame in RGB565 is 112.5 KiB; six single-buffered = 675 KiB,
exceeding the RP2350's 520 KiB SRAM. The "hold 6 framebuffers" premise was never feasible
and would have forced fragile scanline/partial-buffer firmware. One display per MCU makes
a full double buffer (225 KiB) trivially fit. Trade-off accepted: higher MCU *count*
(72 × RP2040 ≈ $50 vs 12 × RP2350 ≈ $13) in exchange for drastically simpler boards, easier
assembly, and trivial firmware — the right trade for a project bottlenecked on assembly
capacity.

---

## Open items before production

- [ ] Confirm GC9A01 FPC tail spec (pin count / pitch / orientation) from physical samples.
- [ ] **Measure real per-node power** (idle / full white / inrush) — see the main README's
      [Phase 1a](../README.md#phase-1a--display-performance-breadboard); replace placeholders.
- [ ] Verify local rail split: backlight on raw 5 V, logic-only through the 3.3 V LDO.
- [ ] Choose exact transceiver P/N and confirm LCSC stock (THVD1450 vs SN65HVD75).
- [ ] Finalize bus connector (pin count, current rating, polarization, strain relief).
- [ ] Power-distribution plan for 72 nodes (injection points, trace/connector current).
- [ ] Pull a live JLCPCB quote with the finalized BOM.
- [ ] Decide baud rate vs. max bus length for the largest planned installation.
