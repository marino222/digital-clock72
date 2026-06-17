# Digital Kinetic Art Clock 

This project is the digital successor to my [ClockClock24 Project](https://github.com/marino222/clockclock24-replica), a replica of the kinetic art piece by [Humans Since 1982](https://www.humanssince1982.com/en-int). The original features physical clock hands driven by stepper motors, which brings challenges around power supply and makes scaling difficult.

This version replaces the clock hands with round LCD displays, which opens up new possibilities beyond just showing the time: animations, a snake game across all displays, or custom widgets. Since the digital version draws significantly less current and is expected to cost less overall, the target scale is a 12×6 display array rather than the 8×3 clocks of the predecessor.

> **Status:** In active design / hardware bring-up. The software components are conceptual;
> the node PCB is in design. See [Roadmap](#roadmap) and [Component status](#repository-layout--component-status).

---

## Table of contents

- [Vision](#vision)
- [Concept at a glance](#concept-at-a-glance)
- [Key decisions](#key-decisions)
- [How it works](#how-it-works)
- [Repository layout & component status](#repository-layout--component-status)
- [Roadmap](#roadmap)
- [Open questions](#open-questions)

---

## Vision

My goal is to build a wall mounted array of round LCD displays that act together as one big synchronized screen. Each display sits in its own spot in a grid, similar to the clocks in the predecessor, but instead of physical hands every spot now has its own small screen.

At first, the array will simply show the time. But since every spot is now a real screen and not just a pair of clock hands, I plan to add more features over time. This includes smooth animations where the hands ease in and out instead of jumping, geometric patterns that move across the whole grid, a snake game that can be played across all displays at once, and small widgets like weather or notifications.

One central computer (likely a Raspberry Pi) will control the whole array and decide what should be shown. It sends this information to every display, and each display takes care of showing its own part of the picture.

---

## Concept at a glance

Here is the short version, before going into the details.

| | |
|---|---|
| **What** | A scalable grid of 1.28" round LCD displays, each with its own microcontroller |
| **Master controller** | One Raspberry Pi 3 running C# / .NET 8, broadcasting global state at 60 FPS |
| **Slave node** | 1× bare RP2040 + 1× GC9A01 round LCD on a small custom PCB |
| **Bus** | Wired half-duplex **RS485**, daisy-chained node to node |
| **Principle** | One identical, fully JLCPCB-assembleable board, replicated N times |

---

## Key decisions

The highest-leverage project-level choices. Hardware-specific decisions (MCU, display,
comms transceiver, power rails, assembly method, etc.) are logged in
[`Node_PCB/README.md`](Node_PCB/README.md) next to the rationale they belong to.

| Decision | Choice | Primary reason |
|---|---|---|
| Overall architecture | Hybrid **"brain + many renderers"** (master/slave) | Real-time pixel timing and global C#/web/game logic each belong on different hardware |
| Master platform | **Raspberry Pi 3**, headless Linux, C# / .NET 8, Dockerized | Heavy global logic + web UI is awkward on an MCU, comfortable on a Pi |
| Master resilience | **Read-only filesystem (OverlayFS)** | Random power cuts must not corrupt the SD card — appliance behaviour |
| Coordination model | **State broadcast, not pixel streaming** | Tiny packets vs. huge frame streams; sync lives at the state layer; scales cleanly from 9 → 72 |
| Broadcast rate | **60 Hz** global-state packets | Frame-locked smooth animation across the array |
| Applications | Time, geometric/animated patterns, array-wide games (Snake) | All are just different *global states* the master broadcasts |
| Target scale | **3×3 (9) pilot → 24 → 72** | "One identical node, replicated" — scale is a quantity decision, not a redesign |

---

## How it works

To make the vision above work, the project splits the thinking and the drawing onto different controllers. One controller calculates the global image/animation and broadcasts these to the nodes. Each node controller then renders its individual pixels and displays them.

```
        ┌───────────────────────────────────────────────┐
        │  MASTER (Raspberry Pi 3)                      │
        │  • Headless Linux, C#                         │
        │  • Time sync, global vector math, game logic  │
        │  • Web UI for control/config                  │ 
        └───────────────────────┬───────────────────────┘
                                │  RS485 bus (broadcast)
                                │  tiny state packets @ 60 FPS
        ┌───────────┬───────────┼───────────┬───────────┐
        ▼           ▼           ▼           ▼           ▼
     ┌──────┐    ┌──────┐    ┌──────┐    ┌──────┐    ┌──────┐
     │ NODE │    │ NODE │    │ NODE │    │ NODE │    │ NODE │  
     │RP2040│    │  +   │    │  +   │    │  +   │    │  +   │
     │GC9A01│    │ LCD  │    │ LCD  │    │ LCD  │    │ LCD  │
     └──────┘    └──────┘    └──────┘    └──────┘    └──────┘
   Each node renders its own pixels locally from the shared global state.
```

### The master and the slave nodes

One computer, the master controller, runs the whole show. It keeps track of time, decides what the array should be displaying, and runs a simple web page so it can be controlled.

Every screen has its own slave controller, together they make up a node. Each node holds the picture for its own screen and draws it fast enough to keep up with all the others. It also knows its own position in the grid, so it only draws its own little piece of the bigger picture.

The work is split this way because one controller can think for the whole array, but it cannot draw fast enough on dozens of screens by itself. And a tiny slave controller is fast enough to draw one screen, but it is too small to run all the planning logic. Splitting the work lets both sides do what they are good at.

There is also a hard numeric reason the nodes have to render locally, not just aconvenience. One screen is 240×240 pixels at 16-bit color (RGB565), so one full frame is 240 × 240 × 2 bytes ≈ **112.5 KiB**. The RP2040 on each node has 264 KiB of SRAM, so it comfortably fits a double buffer for its *own* screen (≈225 KiB) with room to spare. An earlier design idea was to create a cluster PCB, holding six displays and one MCU. This wouldn't work since the six buffer frames needed, would exceed the RP2040s SRAM. That is the actual reason the project is one MCU per display rather than one MCU per several displays. 

Read more about the master controller in [`Master_Engine/README.md`](Master_Engine/README.md), about the nodes in [`Slave_Firmware/README.md`](Slave_Firmware/README.md), and about the
hardware behind them in [`Node_PCB/README.md`](Node_PCB/README.md).

### How the data is sent to the nodes

Here is the idea that makes the whole array possible. The brain does not send a full picture to every screen, sixty times a second. That would be far too much data. Instead, it sends a tiny instruction, something like "the hand should now point at 45 degrees" or "the snake is now at position 12, 2." Every node controller receives the exact same tiny instruction at the exact same moment, and draws its own piece of the picture from it.

The numbers explain why this is not just a nice idea but the only one that works. Streaming one full frame to one screen at 60 FPS is 112.5 KiB × 60 ≈ **6.6 MB/s**, for a single
screen alone. For 9 nodes that is already ≈ 60 MB/s, at the full 72-node array it is ≈ 475 MB/s. A wired RS485 bus, the kind of connection this project uses, realistically carries somewhere in the low single-digit Mbit/s over a long daisy chain, several hundred times too little. A state instruction, on the other hand, is a handful of bytes sent once, at 60 Hz, no matter how many screens are listening. That gap is the entire reason the master only ever sends instructions, never pixels.

This has three nice side effects. It needs very little data, since an instruction is just a handful of bytes instead of a full image. It keeps everything in sync automatically, since every screen hears the same instruction at the same time. And it scales easily, since adding more screens does not change how much data the brain has to send.

To reliably send state instruction a Data Protocol needs to be defined. Read more about this in [`Master_Engine/README.md`](Master_Engine/README.md) and [`Slave_Firmware/README.md`](Slave_Firmware/README.md).

### What actually will be displayed

This instruction can describe anything, which is what makes the project exciting. So far the plan includes:

- **Time:** the original purpose, digits and clock faces drawn across the grid.
- **Geometric and animated patterns:** motion across the whole grid, with easing animations instead of sudden jumps.
- **A snake game:** one game played across the entire array at once.
- **Small widgets:** controlled from the brain's simple web page.

---

## Repository layout & component status

```
digital-clock72/
├── README.md                 ← you are here (project-wide entry point & live doc)
├── LICENSE                   ← MIT
├── Master_Engine/             ← Gen-2 C#/.NET 8 Dockerized "brain" + web UI (conceptual)
├── Slave_Firmware/            ← Gen-2 RP2040 rendering firmware, PlatformIO (conceptual)
├── Node_PCB/                  ← Gen-2 single-node KiCad project + full hardware doc (in design)
└── docs/                      ← images & datasheets only (created once the first asset exists)
```

| Path | Tech | Role | Status |
|---|---|---|---|
| **Master_Engine/** | C# / .NET 8, Docker, on Pi 3 | Global logic, time sync, vector math, games, web UI, RS485 broadcast | Conceptual |
| **Slave_Firmware/** | C++ (PlatformIO), RP2040 | Framebuffer + SPI/DMA rendering, RS485 receive, local draw, auto-addressing | Conceptual |
| **Node_PCB/** | KiCad | Single-node board (RP2040 + GC9A01 FPC + RS485 + power + USB-C/SWD) | In design |

Each component folder has its own README. `Master_Engine/` and `Slave_Firmware/` keep
theirs short (status + intended scope) since both are still conceptual; `Node_PCB/` carries
the full hardware architecture write-up since that design work is actually underway.

---

## Roadmap

This is the project's running progress tracker — extend it as phases complete.

1. **Prove a single display** at 60 FPS (Pico + dev board) and **measure power**.
2. **Prove the bus** — two MCUs over RS485, master→slave state broadcast.
3. **Prove one custom node board**, then a **3×3 pilot** as a full system test.
4. **Stand up the Master_Engine** (C#/.NET 8) with time sync + one real mode (clock).
5. **Implement the broadcast protocol + auto-addressing** end-to-end on the pilot.
6. **Scale** to 24, then 72, applying measured power and injection-point rules.
7. **Add applications** (patterns, Snake) and the web UI on top of the proven transport.

The detailed, phase-by-phase **hardware bring-up plan** (breadboard → FPC samples →
single-node board → 3×3 pilot → scale, each with its own pass/fail gate) lives in
[`Node_PCB/README.md`](Node_PCB/README.md), since it's specific to the node hardware.

---

## Open questions

- **Final scope / scale commitment.** Pilot → 24 → 72 is confirmed as the path; the full 72
  is **gated on pilot results and assembly capacity**.
- **Power at scale.** The dominant unknown. Resolved by *early measurement* (Phase 1a) —
  see the hardware checklist in [`Node_PCB/README.md`](Node_PCB/README.md). Every power
  figure in that doc is a placeholder until then.
- **Broadcast protocol definition.** Exact packet format and per-mode state schema (clock,
  pattern, Snake) — to be designed alongside `Master_Engine` and `Slave_Firmware`.
- **Addressing scheme.** Daisy-chain auto-addressing chosen over DIP switches; to be
  *implemented* in firmware.
- **Time-sync precision.** How tightly nodes must align for visually seamless animation.

The hardware-specific pre-production checklist (FPC tail confirmation, transceiver part
number, bus connector, power distribution plan, etc.) lives in
[`Node_PCB/README.md`](Node_PCB/README.md).

---

