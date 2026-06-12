# Digital Kinetic Clock Array (`digital-clock72`)

A wall-mounted **array of round LCD displays** that act as a single coordinated canvas —
showing the time, geometric/animated patterns, and array-wide games (e.g. Snake crawling
across the whole grid) at a smooth, **synchronized 60 FPS** on every display.

It is the **digital successor** to a completed mechanical kinetic clock (a ClockClock-style
144-stepper / 72-clock array). Trading moving parts for pixels removes mechanical wear and
massively expands what the array can show — any graphic, not just clock hands.

> **Status:** In active design / hardware bring-up. The software components are conceptual;
> the node PCB is in design. See [Roadmap](#roadmap) and [Status](#component-status).

---

## Table of contents

- [Concept at a glance](#concept-at-a-glance)
- [System architecture](#system-architecture)
- [The core idea — state broadcast, not pixel streaming](#the-core-idea--state-broadcast-not-pixel-streaming)
- [Repository layout](#repository-layout)
- [Component status](#component-status)
- [Key decisions](#key-decisions)
- [Roadmap](#roadmap)
- [Open questions](#open-questions)
- [Documentation](#documentation)
- [Getting started](#getting-started)
- [License](#license)

---

## Concept at a glance

| | |
|---|---|
| **What** | A scalable grid of 1.28" round LCD "pixels," each its own microcontroller node |
| **Brain** | One Raspberry Pi 3 running C# / .NET 8, broadcasting global state at 60 Hz |
| **Node** | 1× bare RP2040 + 1× GC9A01 round LCD on a small custom PCB |
| **Bus** | Wired half-duplex **RS485** multidrop, daisy-chained node to node |
| **Scale** | **3×3 (9-node) pilot → 24 → 72**, architecturally scalable to ~250 with no redesign |
| **Principle** | One identical, fully JLCPCB-assembleable board, replicated N times |

---

## System architecture

A hybrid **"brain + many renderers"** design. High-level logic lives in one capable
computer; pixel rendering is distributed to one small microcontroller per display.

```
        ┌───────────────────────────────────────────────┐
        │  MASTER — "The Brain"  (Raspberry Pi 3)         │
        │  • Headless Linux, C# / .NET 8 (Dockerized)     │
        │  • Time sync, global vector math, game logic    │
        │  • Web UI for control/config                     │
        │  • Read-only filesystem (OverlayFS) for power-   │
        │    cut resilience (appliance behaviour)          │
        └───────────────────────┬───────────────────────-┘
                                │  RS485 bus (broadcast)
                                │  tiny state packets @ 60 Hz
        ┌───────────┬───────────┼───────────┬───────────┐
        ▼           ▼           ▼           ▼           ▼
     ┌──────┐    ┌──────┐    ┌──────┐    ┌──────┐    ┌──────┐
     │ NODE │    │ NODE │    │ NODE │    │ NODE │ …  │ NODE │   (9 → 24 → 72)
     │RP2040+│   │  +   │    │  +   │    │  +   │    │  +   │
     │GC9A01 │    │ LCD  │    │ LCD  │    │ LCD  │    │ LCD  │
     └──────┘    └──────┘    └──────┘    └──────┘    └──────┘
   Each node renders its own pixels locally from the shared global state.
```

- **Master ("Brain") — Raspberry Pi 3.** Owns everything *global*: wall-clock time and
  sync, the math deciding what the whole array does, game state, and a web UI. A read-only
  OverlayFS root makes random power cuts safe — it behaves like an appliance, not a PC.
- **Slaves ("Renderers") — one MCU per display.** Each node holds a full double framebuffer
  locally, renders at 60 FPS over SPI + DMA, reads its own address from the daisy chain,
  and draws only its slice of the global picture.

**Why the split:** a Linux Pi can't do real-time microsecond pixel timing across many
displays; a microcontroller can't comfortably host the global C#/web/game logic. Splitting
the two plays to each one's strength.

Full system context → [`docs/PROJECT_OVERVIEW.md`](docs/PROJECT_OVERVIEW.md).
Full node hardware spec → [`docs/PCB_ARCHITECTURE.md`](docs/PCB_ARCHITECTURE.md).

---

## The core idea — state broadcast, not pixel streaming

The single most important architectural decision: **the master does not stream pixels.**
It broadcasts **tiny state packets** (e.g. `[Global_Angle: 45]`, `[Snake_X: 12, Snake_Y: 2]`)
at 60 Hz over the bus. Each node intercepts the global state, reads its own ID, and
**renders the actual pixels locally**.

- **Bandwidth:** streaming full frames to dozens of 240×240 displays is enormous; a state
  packet is a handful of bytes.
- **Sync:** synchronization lives at the *state* layer, so adding nodes doesn't hurt sync —
  every node receives the same broadcast at the same instant.
- **Scalability:** going from 9 to 72 renderers changes nothing about the broadcast.

Everything the array displays — time, patterns, games — is just a different *global state*
the master broadcasts; nodes render whatever slice matches their position.

---

## Repository layout

```
digital-clock72/
├── README.md                 ← you are here (system & vision entry point)
├── LICENSE                   ← MIT
├── CONTRIBUTING.md
├── docs/
│   ├── PROJECT_OVERVIEW.md   ← system- & vision-level context (canonical)
│   ├── PCB_ARCHITECTURE.md   ← node hardware spec, BOM, cost, bring-up (canonical)
│   ├── DECISIONS.md          ← consolidated decision log (system + hardware)
│   ├── ROADMAP.md            ← combined roadmap + hardware bring-up phases
│   └── OPEN_QUESTIONS.md     ← open questions & items to resolve before production
├── Master_Engine/            ← Gen-2 C#/.NET 8 Dockerized "brain" + web UI (conceptual)
├── Slave_Firmware/           ← Gen-2 RP2040 rendering firmware, PlatformIO (conceptual)
└── Node_PCB/                 ← Gen-2 single-node KiCad project (in design)
```

The two files in `docs/` are the **canonical, live source documents**; the sections in this
README summarize them and link out. `DECISIONS.md`, `ROADMAP.md`, and `OPEN_QUESTIONS.md`
consolidate material that is split across both source docs into single readable views.

> **Gen-1 (mechanical) predecessor.** The completed ClockClock-style mechanical array
> (`clockclock24-replica/` — 144 steppers / 72 clocks, full KiCad + PlatformIO + STLs) is
> referenced as the project's predecessor but is **not** imported here; it lives in its own
> project. This repository is Gen-2 (digital).

---

## Component status

| Component | Tech | Role | Status |
|---|---|---|---|
| **Master_Engine/** | C# / .NET 8, Docker, on Pi 3 | Global logic, time sync, vector math, games, web UI, RS485 broadcast | Conceptual |
| **Slave_Firmware/** | C++ (PlatformIO), RP2040 | Framebuffer + SPI/DMA rendering, RS485 receive, local draw, auto-addressing | Conceptual |
| **Node_PCB/** | KiCad | Single-node board (RP2040 + GC9A01 FPC + RS485 + power + USB-C/SWD) | In design |
| Gen-1 mechanical | KiCad, PlatformIO, STL | Predecessor (144 steppers / 72 clocks) | Complete & functional (separate repo) |

> **Architecture note:** the earlier "Smart Tile" concept (one MCU driving six displays)
> was **superseded** by the **one-MCU-per-display** model — six full framebuffers don't fit
> in one MCU's SRAM. The node-PCB design reflects the simpler, more scalable approach.
> Rationale in [`docs/PCB_ARCHITECTURE.md`](docs/PCB_ARCHITECTURE.md) §1.

---

## Key decisions

The highest-leverage choices, summarized. Full rationale and the complete log live in
[`docs/DECISIONS.md`](docs/DECISIONS.md).

| Decision | Choice | Primary reason |
|---|---|---|
| Topology | 1 MCU per display | Framebuffer RAM; trivial firmware; modular scaling |
| MCU | Bare RP2040 (LCSC C2040) | No radio → no RF/cert; cheap; JLC-placeable; enough RAM |
| Display | GC9A01 1.28", **FPC tail** | No 2nd PCB; flat mount; clean assembly |
| Frame rate | 60 FPS, DMA + double buffer | Free per-display once RAM pressure is gone |
| Comms | RS485, **1/8-unit-load transceiver** | Deterministic, native broadcast, scales to ~256 |
| Master | Raspberry Pi 3, C#/.NET 8, Docker | Global logic awkward on an MCU, comfortable on a Pi |
| Coordination | State broadcast, not pixel streaming | Tiny packets; sync at the state layer; scales cleanly |
| Addressing | Daisy-chain auto-address (firmware) | No DIP switches; scales to any count |
| Flashing | USB-C + BOOT (+ SWD pads) | PlatformIO workflow; SWD for recovery/debug |
| Assembly | Full JLCPCB turnkey SMT | All parts in LCSC; no hand soldering |

---

## Roadmap

Project-level milestones (detailed hardware bring-up phases in
[`docs/ROADMAP.md`](docs/ROADMAP.md)):

1. **Prove a single display** at 60 FPS (Pico + dev board) and **measure power**.
2. **Prove the bus** — two MCUs over RS485, master→slave state broadcast.
3. **Prove one custom node board**, then a **3×3 pilot** as a full system test.
4. **Stand up the Master_Engine** (C#/.NET 8) with time sync + one real mode (clock).
5. **Implement the broadcast protocol + auto-addressing** end-to-end on the pilot.
6. **Scale** to 24, then 72, applying measured power and injection-point rules.
7. **Add applications** (patterns, Snake) and the web UI on top of the proven transport.

**Bring-up sequence at a glance:**
`1a display perf → 1b RS485 link (+ measure power) → 2 FPC samples → 3 single-node board → 4 3×3 pilot → 5 scale`

---

## Open questions

Tracked in full (with hardware checklist) in
[`docs/OPEN_QUESTIONS.md`](docs/OPEN_QUESTIONS.md). The big ones:

- **Power at scale** — the dominant unknown; resolved by *early measurement*. All power
  numbers in the docs are placeholders until measured in Phase 1a.
- **Broadcast protocol definition** — exact packet format and per-mode state schema (clock,
  pattern, Snake), to be designed alongside Master_Engine and Slave_Firmware.
- **Time-sync precision** — how tightly nodes must align for visually seamless animation.
- **GC9A01 FPC tail spec** — pin count / pitch / contact orientation must be confirmed from
  *physical samples* before the PCB is finalized (the most common mistake on this board type).
- **Final scope** — pilot → 24 → 72 is the path; full 72 gated on pilot results and
  assembly capacity.

---

## Documentation

| Document | What it covers |
|---|---|
| [`docs/PROJECT_OVERVIEW.md`](docs/PROJECT_OVERVIEW.md) | System- and vision-level context: history, architecture, software, roadmap |
| [`docs/PCB_ARCHITECTURE.md`](docs/PCB_ARCHITECTURE.md) | Node hardware spec: topology, MCU, display, comms, power, BOM/cost, assembly, bring-up |
| [`docs/DECISIONS.md`](docs/DECISIONS.md) | Consolidated decision log with rationale (system + hardware) |
| [`docs/ROADMAP.md`](docs/ROADMAP.md) | Combined project milestones and phased hardware bring-up plan |
| [`docs/OPEN_QUESTIONS.md`](docs/OPEN_QUESTIONS.md) | Open questions and the checklist of items to resolve before production |

---

## Getting started

The hardware and firmware are still in design — there is nothing to build or flash yet.
For now this repo is the **design home**. To follow or contribute to the design:

1. Read [`docs/PROJECT_OVERVIEW.md`](docs/PROJECT_OVERVIEW.md) for the vision and system model.
2. Read [`docs/PCB_ARCHITECTURE.md`](docs/PCB_ARCHITECTURE.md) for the node hardware.
3. See [`docs/OPEN_QUESTIONS.md`](docs/OPEN_QUESTIONS.md) for what still needs deciding.

Each component folder (`Master_Engine/`, `Slave_Firmware/`, `Node_PCB/`) has its own README
describing its intended scope and current status.

---

## License

[MIT](LICENSE) — see the `LICENSE` file. Applies to the software, firmware, and hardware
design files in this repository.
