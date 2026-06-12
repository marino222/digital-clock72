# PROJECT_OVERVIEW.md — Digital Kinetic Clock Array

> **Scope of this file.** This is the *system- and vision-level* context for the whole
> project. It covers where the project came from, the overall master/slave architecture,
> the software and applications, and the roadmap. The detailed **node hardware** spec
> (PCB, BOM, assembly, bring-up plan) lives in its companion file **`PCB_ARCHITECTURE.md`**
> and is referenced, not duplicated, here.

---

## 1. Vision

Build a wall-mounted **array of round LCD displays** that act as a single coordinated
canvas — showing the time, geometric/animated patterns, and array-wide graphical games
(e.g. Snake that crawls across the whole grid) at smooth **60 FPS, synchronized** across
every display.

It is the **digital successor** to a completed mechanical kinetic clock — trading moving
parts for pixels, which removes mechanical wear and constraints while massively expanding
what the array can display (any graphic, not just clock hands).

**Target scale:** modular and scalable by design. Concretely: a **3×3 (9-node) pilot**,
growing to **24**, with **72** as the full-array goal. The architecture is deliberately
"one identical node, replicated," so scale is a quantity decision, not a redesign.

---

## 2. Background — From Mechanical to Digital

| Generation | What it was | Status |
|---|---|---|
| **Gen 1 — Mechanical** | A ClockClock-style kinetic array: **144 stepper motors / 72 clocks**, with the "hands" forming digits and patterns. Full KiCad, PlatformIO firmware, and 3D-printed parts. | **Complete & functional** (`clockclock24-replica/`) |
| **Gen 2 — Digital (this project)** | Replace each mechanical clock with a **1.28" round GC9A01 LCD** driven by its own microcontroller, coordinated over a wired bus. | **In active design / bring-up** |

The move to displays is what unlocks the larger ambition: arbitrary graphics, games, and
animation that mechanical hands could never show.

---

## 3. System Architecture — Master / Slave

The system is a **hybrid "brain + many renderers"** design. High-level logic lives in one
capable computer; pixel rendering is distributed to one small microcontroller per display.

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
     │ RP2040+│   │  +   │    │  +   │    │  +   │    │  +   │
     │GC9A01 │    │ LCD  │    │ LCD  │    │ LCD  │    │ LCD  │
     └──────┘    └──────┘    └──────┘    └──────┘    └──────┘
   Each node renders its own pixels locally from the shared global state.
```

### 3.1 The Master ("Brain") — Raspberry Pi 3
- Runs **C# / .NET 8**, **Dockerized**, headless Linux.
- Owns everything *global*: wall-clock time and sync, the math that decides what the whole
  array is doing, game state, and a **web UI** for control and configuration.
- Uses a **read-only filesystem (OverlayFS)** so random power cuts don't corrupt the SD
  card — it behaves like an appliance, not a PC.
- Chosen because heavy global C#/web/game logic is awkward on a microcontroller, while a
  Pi handles it comfortably.

### 3.2 The Slaves ("Renderers") — one MCU per display
- Each node = **1× bare RP2040 + 1× GC9A01 1.28" display** on a small custom PCB.
- Holds a **full double framebuffer** locally and renders at 60 FPS via SPI + DMA.
- Reads its **position/address** from the daisy-chain and renders only its slice of the
  global picture.
- Full hardware detail, rationale, cost, and assembly → **`PCB_ARCHITECTURE.md`**.

### 3.3 Why this split
A standard Linux Pi can't do real-time microsecond pixel timing across many displays, and
a microcontroller can't comfortably host the global C#/web/game logic. Splitting the two
plays to each one's strength.

---

## 4. The Core Idea — State Broadcast, Not Pixel Streaming

The single most important architectural decision. **The master does not stream pixels.**
It broadcasts **tiny state packets** (e.g. `[Global_Angle: 45]`, `[Snake_X: 12, Snake_Y: 2]`)
at 60 Hz over the bus. Each node intercepts the global state, reads its own ID, and
**renders the actual pixels locally**.

**Why this is the right model:**
- **Bandwidth:** streaming full frames to dozens of 240×240 displays is enormous; a state
  packet is a handful of bytes.
- **Sync:** synchronization lives at the *state* layer, so adding more nodes doesn't hurt
  sync — every node receives the same broadcast at the same instant.
- **Scalability:** going from 9 to 72 renderers changes nothing about the broadcast; it's
  the property that makes the array scale cleanly.

**Transport:** a wired **RS485** multidrop bus — deterministic low-jitter timing, native
broadcast (one transmit reaches all nodes), and noise-immune. Chosen over WiFi, whose
latency jitter and contention are unacceptable for frame-locked sync. (Transceiver choice,
termination, addressing → `PCB_ARCHITECTURE.md`.)

---

## 5. Applications (What the Array Displays)

- **Time:** the primary function — digits/clock faces rendered across the grid.
- **Geometric & animated patterns:** array-wide motion, angles, waves (the kind of
  "kinetic art" the mechanical version did, now unconstrained by physical hands).
- **Global games:** a single game spanning all displays — **Snake** is the reference
  example, with the playfield mapped across the whole array.
- **Web-driven control:** mode/config via the master's web UI.

All of these are just different *global states* the master broadcasts; the nodes render
whatever slice corresponds to their position.

---

## 6. Software / Firmware Components

| Component | Tech | Role | Status |
|---|---|---|---|
| **Master_Engine/** | C# / .NET 8, Docker, on Pi 3 | Global logic, time sync, vector math, game logic, web UI, RS485 broadcast | Conceptual |
| **Slave_Firmware/** | C++ (PlatformIO), RP2040 | Framebuffer + SPI/DMA rendering, RS485 receive, local draw from global state, auto-addressing | Conceptual |
| **Smart_Tile_PCB/** → **Node PCB** | KiCad | Single-node board (RP2040 + display FPC + RS485 + power + USB-C/SWD) | In design — see `PCB_ARCHITECTURE.md` |

> **Architecture note:** the earlier "Smart Tile" concept (one MCU driving six displays)
> was **superseded** by the **one-MCU-per-display** model after the framebuffer-RAM math
> showed six full buffers don't fit in one MCU's SRAM. The node-PCB design reflects the
> newer, simpler, more scalable approach. Rationale in `PCB_ARCHITECTURE.md` §1.

---

## 7. Repository / File Map

| Path | Contents | Status |
|---|---|---|
| `clockclock24-replica/` | Gen-1 mechanical: KiCad, PlatformIO firmware, STL files | **Complete & functional** |
| `Master_Engine/` | Gen-2 C#/.NET 8 Dockerized brain + web UI | Conceptual |
| `Slave_Firmware/` | Gen-2 RP2040 rendering firmware (PlatformIO) | Conceptual |
| `Node_PCB/` (was `Smart_Tile_PCB/`) | Gen-2 single-node KiCad project | In design |
| `PCB_ARCHITECTURE.md` | Node hardware spec, BOM, cost, assembly, bring-up plan | **Live** |
| `PROJECT_OVERVIEW.md` | This file — system & vision context | **Live** |

---

## 8. Roadmap (System Level)

The detailed hardware bring-up phases are in `PCB_ARCHITECTURE.md` §10. At the *project*
level the milestones are:

1. **Prove a single display** at 60 FPS (Pico + dev board) and **measure power**.
2. **Prove the bus** (two MCUs over RS485, master→slave state broadcast).
3. **Prove one custom node board**, then a **3×3 pilot** as a full system test.
4. **Stand up the Master_Engine** (C#/.NET 8) with time sync + one real mode (clock).
5. **Implement the broadcast protocol + auto-addressing** end-to-end on the pilot.
6. **Scale** to 24, then 72, applying measured power and injection-point rules.
7. **Add applications** (patterns, Snake) and the web UI on top of the proven transport.

---

## 9. Key Open Questions (Project Level)

- **Final scope/scale commitment:** pilot → 24 → 72 confirmed as the path; full 72 gated
  on pilot results and assembly capacity.
- **Power at scale:** the dominant unknown; resolved by early measurement (see hardware doc).
- **Broadcast protocol definition:** exact packet format and per-mode state schema (clock,
  pattern, Snake) — to be designed alongside Master_Engine and Slave_Firmware.
- **Addressing scheme:** daisy-chain auto-addressing chosen over DIP switches; to be
  implemented in firmware.
- **Time-sync precision:** how tightly nodes must align for visually seamless animation.

---

## 10. One-Paragraph Summary

A scalable wall array of round LCD "pixels," each its own RP2040 + GC9A01 node, coordinated
by a Raspberry Pi 3 "brain" running C#/.NET 8. The brain broadcasts tiny global-state
packets at 60 Hz over a wired RS485 bus; each node renders its own pixels locally and in
sync. It is the digital successor to a completed mechanical kinetic clock, built to display
time, patterns, and array-wide games, and engineered so that scaling from a 9-node pilot to
a 72-node array is a matter of replicating one identical, fully JLCPCB-assembleable board.
