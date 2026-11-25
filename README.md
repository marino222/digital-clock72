# Digital Kinetic Art Clock 

This project is the digital successor to my [ClockClock24 Project](https://github.com/marino222/clockclock24-replica), a replica of the kinetic art piece by [Humans Since 1982](https://www.humanssince1982.com/en-int). The original features physical clock hands driven by stepper motors, which brings challenges around power supply and makes scaling difficult.

This version replaces the clock hands with round LCD displays, which opens up new possibilities beyond just showing the time.


---

## Table of contents

- [Vision](#vision)
- [Concept](#concept)
- [How it works](#how-it-works)
- [Repository layout & component status](#repository-layout)
- [Roadmap](#roadmap)
- [Open questions](#open-questions)

---

<a id="vision"></a>
## 🌅 Vision

The goal is an array of round LCD displays that act together as one big synchronized screen. Each display sits in its own spot in a grid, similar to the clocks in the predecessor, but instead of physical hands every spot now has its own small screen.

At first, the array will simply show the time. Since every spot is a real screen, more features like widgets or a snake game can be added easily.

Two sizes are planned: a **24-node desktop version** and a **72-node wall version**. One central controller decides what should be shown and sends it to every display. Each display then draws its own part of the picture.

---

<a id="concept"></a>
## 🧩 Concept 

| Aspect | Choice | Why |
|---|---|---|
| Architecture | Master for calculation, slaves for rendering | Real-time pixel drawing and global logic need different hardware |
| Master | ESP32-S3 in C++, broadcasts state at 60 Hz | Plenty for 72 nodes, exact timing, boots instantly, shares the protocol code with the nodes |
| Node (slave) | 1× bare RP2040 + 1× GC9A01 round LCD, on a small custom PCB | One identical board, JLCPCB assembled, just replicated |
| Bus | RS485 at 1 Mbaud, daisy chained node to node | Carries tiny state packets instead of full frames, so it scales cleanly |
| Mechanics | 3D printed brackets, front plate printed or laser-cut acrylic | Cheap to iterate, no screws per node |

---

<a id="how-it-works"></a>
## ⚙️ How it works

The master calculates what the whole array should show and broadcasts it as a small state packet. Each node receives the same packet, picks out its own part and renders its own pixels.

```
        ┌───────────────────────────────────────────────┐
        │  MASTER (ESP32-S3)                            │
        │  • C++    │
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

Why the nodes have to render locally comes down to three numbers:

- **One frame is 112.5 KiB.** 240 × 240 pixels at 16-bit color. The RP2040's 264 KiB of SRAM fits a double buffer for its own screen, but no more.
- **Streaming pixels would need ≈ 475 MB/s** for 72 screens at 60 FPS. The RS485 bus carries about 1 Mbit/s, several hundred times too little.
- **A state packet for all 72 nodes is at most 295 bytes.** At 60 Hz that uses about 18 % of the bus, no matter how many screens are listening.

Because every node hears the same packet at the same time, the array stays in sync automatically. The packet format is described in [`tests/`](tests/README.md) (phase 3.1). The software principles are in [`software/README.md`](software/README.md), more on the master in [`software/master/README.md`](software/master/README.md), the nodes in [`software/slave/README.md`](software/slave/README.md), the hardware in [`pcb/README.md`](pcb/README.md) and the mechanics in [`mechanical/README.md`](mechanical/README.md).

### What will be displayed

- **Time:** the original purpose, digits and clock faces drawn across the grid.
- **Geometric and animated patterns:** motion across the whole grid, with easing instead of sudden jumps.
- **Small widgets:** controlled from the master's web page.

---

<a id="repository-layout"></a>
## 📦 Repository layout & component status

| Path | Tech | Role | Status |
|---|---|---|---|
| [`software/`](software/README.md) | C++ (PlatformIO) | Base principles for master and slave, shared RS485 protocol | Principles defined |
| [`software/master/`](software/master/README.md) | C++ (PlatformIO), ESP32-S3 | Global logic, time sync, animations, games, web UI, RS485 broadcast | Platform chosen, not started |
| [`software/slave/`](software/slave/README.md) | C++ (PlatformIO), RP2040 | Framebuffer + SPI/DMA rendering, RS485 receive, auto-addressing | Test firmware in `tests/` |
| [`pcb/`](pcb/README.md) | KiCad | Node board: RP2040, display connector, RS485, power, USB-C/SWD | v1.0 built and verified |
| [`mechanical/`](mechanical/README.md) | Onshape | Brackets, front plate, PCB outline constraints | 2×2 prototype in progress |
| [`power/`](power/README.md) | — | Power supply and wiring, based on measured values | Measured, wiring defined |
| [`tests/`](tests/README.md) | — | Test procedures and results, one section per phase | Ongoing |
| [`tools/`](tools/) | HTML | Helper tools, e.g. a to-scale [mockup](tools/clock-grid-mockup.html) of the front with clock-hand animations | Draft |
| `docs/` | — | Datasheets and images | — |

---

<a id="roadmap"></a>
## 🗺️ Roadmap

The approach is to test cheap, risky parts first before committing to PCB production. The prototype is a **2×2 (4-node)** array, large enough to validate the bus, addressing and power distribution without high cost. Details and results for each phase are in [`tests/`](tests/README.md).

| Phase | Goal | Status |
|---|---|---|
| 1 | Run a single display at 60 FPS and measure its power | ✅ Completed |
| 2 | Prove the RS485 link with a disposable test sender | ✅ Completed |
| 3 | Prove the state protocol and slave firmware on breadboards | ✅ Completed |
| 4 | Confirm the display hardware | ✅ Completed |
| 5 | Build the 2×2 prototype | ⚙ In progress |
| 5.1 | Design the node PCB, order 5 boards, bring up and measure one | ✅ Completed |
| 5.2a | Mechanical assembly: brackets, cover rings, front plate | ⚙ In progress |
| 5.2b | Firmware on the 2×2: auto-addressing, master v1 on ESP32-S3, sync, voltage drop along the chain | ⬜ Not started |
| 6 | Scale to the 24-node desktop version, then the 72-node wall version | ⬜ Not started |
| 7 | Add applications and the web UI: patterns, Snake, widgets | ⬜ Not started |

---

<a id="open-questions"></a>
## ❓ Open questions

Only questions that affect the whole project are listed here. Open points of a single component are in its own README, for example [power/](power/README.md#open-points).

- **RS485 topology at 72 nodes.** Several branches from one master. Likely one transceiver per branch, so each branch stays a clean linear bus.
- **Syncing.** How to make sure all displays run in sync.
