# Digital Kinetic Art Clock 

This project is the digital successor to my [ClockClock24 Project](https://github.com/marino222/clockclock24-replica), a replica of the kinetic art piece by [Humans Since 1982](https://www.humanssince1982.com/en-int). The original features physical clock hands driven by stepper motors, which brings challenges around power supply and makes scaling difficult.

This version replaces the clock hands with round LCD displays, which opens up new possibilities beyond just showing the time: animations, a snake game across all displays, or custom widgets. Since the digital version draws significantly less current and is expected to cost less overall, the target scale is a 12×6 display array rather than the 8×3 clocks of the predecessor.

> **Status:** A work in progress

---

## Table of contents

- [Vision](#vision)
- [Key decisions](#key-decisions)
- [How it works](#how-it-works)
- [Repository layout & component status](#repository-layout--component-status)
- [Roadmap](#roadmap)
- [Open questions](#open-questions)

---

## 🌅 Vision

My goal is to build a wall mounted array of round LCD displays that act together as one big synchronized screen. Each display sits in its own spot in a grid, similar to the clocks in the predecessor, but instead of physical hands every spot now has its own small screen.

At first, the array will simply show the time. But since every spot is now a real screen and not just a pair of clock hands, I plan to add more features over time. This includes smooth animations where the hands ease in and out instead of jumping, geometric patterns that move across the whole grid, a snake game that can be played across all displays at once, and small widgets like weather or notifications.

One central computer (likely a Raspberry Pi) will control the whole array and decide what should be shown. It sends this information to every display, and each display takes care of showing its own part of the picture.

---

## 🧩 Concept 


| Aspect | Choice | Why |
|---|---|---|
| Architecture | Master for calulation / Slave for rendering | Real time pixel drawing and global logic need different hardware |
| Master | Raspberry Pi 3, headless Linux broadcasts state at 60 Hz | Global logic and the web UI run better on a Pi than on a microcontroller |
| Node (Slave) | 1× bare RP2040 + 1× GC9A01 round LCD, on a small custom PCB | One identical board, JLCPCB assembled, just replicated |
| Bus | RS485, daisy chained node to node | Carries tiny state packets instead of full frames, so it scales cleanly |
| Applications | Time, animated patterns, array wide games (Snake) | All just different states broadcast by the master |

---

## ⚙️ How it works

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

One computer, the master controller, runs the whole logic. It keeps track of time, decides what the array should be displaying, and runs a simple web page so it can be controlled. Every screen has its own slave controller, together they make up a node. Each node holds the picture for its own screen and draws it fast enough to keep up with all the others. It also knows its own position in the grid, so it only draws its own little piece of the bigger picture. The work is split this way because one controller can think for the whole array, but it cannot draw fast enough on dozens of screens by itself. And a tiny slave controller is fast enough to draw one screen, but it is too small to run all the planning logic. Splitting the work lets both sides do what they are good at. There is also a hard numeric reason the nodes have to render locally. One screen is 240×240 pixels at 16-bit color (RGB565), so one full frame is 240 × 240 × 2 bytes ≈ **112.5 KiB**. The RP2040 on each node has 264 KiB of SRAM, so it comfortably fits a double buffer for its *own* screen (≈225 KiB) with room to spare.

Read more about the master controller in [`master/README.md`](master/README.md), about the nodes in [`slave/README.md`](slave/README.md), and about the
hardware behind them in [`node_pcb/README.md`](node_pcb/README.md).

### How the data is sent to the nodes

Here is the idea that makes the whole array possible. The master does not send a full picture to every screen, sixty times a second. That would be far too much data. Instead, it sends a tiny instruction, something like "the hand should now point at 45 degrees" or "the snake is now at position 12, 2." Every node controller receives the exact same tiny instruction at the exact same moment, and draws its own piece of the picture from it. Streaming one full frame to one screen at 60 FPS is 112.5 KiB × 60 ≈ **6.6 MB/s**, for a single screen alone. For 9 nodes that is already ≈ 60 MB/s, at the full 72-node array it is ≈ 475 MB/s. A wired RS485 bus, the kind of connection this project uses, realistically carries somewhere in the low single-digit Mbit/s over a long daisy chain, several hundred times too little. A state instruction, on the other hand, is a handful of bytes sent once, at 60 Hz, no matter how many screens are listening. That gap is the entire reason the master only ever sends instructions, never pixels. This has three nice side effects. It needs very little data, since an instruction is just a handful of bytes instead of a full image. It keeps everything in sync automatically, since every screen hears the same instruction at the same time. And it scales easily, since adding more screens does not change how much data the brain has to send.

To reliably send state instruction a Data Protocol needs to be defined. Read more about this in [`master/README.md`](master/README.md) and [`slave/README.md`](slave/README.md).

### What actually will be displayed

This instruction can describe anything, which is what makes the project exciting. So far the plan includes:

- **Time:** the original purpose, digits and clock faces drawn across the grid.
- **Geometric and animated patterns:** motion across the whole grid, with easing animations instead of sudden jumps.
- **A snake game:** one game played across the entire array at once.
- **Small widgets:** controlled from the brain's simple web page.
- **Gifs**

---

## 📦 Repository layout & component status

```
Digital Clock 72/
├── 3D files/
│   ├── final/
│   └── test/
├── docs/
│   ├── datasheets/
│   └── images/
├── master/
│   └── README.md
├── slave/
│   └── README.md
├── node_pcb/
│   ├── manufacturing/
│   └── README.md
├── tests/
│   ├── phase-1.1-display-performance/
│   └── README.md
└── README.md
```

| Path | Tech | Role | Status |
|---|---|---|---|
| **master/** | C# on Pi 3 | Global logic, time sync, vector math, games, web UI, RS485 broadcast | Conceptual |
| **slave/** | C++ (PlatformIO), RP2040 | Framebuffer + SPI/DMA rendering, RS485 receive, local draw, auto-addressing | Conceptual |
| **node_pcb/** | KiCad | Single-node board (RP2040 + GC9A01 display header + RS485 + power + USB-C/SWD) | In design |
| **tests/** | — | Bring-up plan test procedures and recorded results, one folder per phase | Not started |


---

## 🗺️ Roadmap

This roadmap tracks the project progress. The approach is to test cheap, risky parts first before committing to PCB production. The prototype is a **3×3 (9-node)** array, large enough to validate the bus, addressing, and power distribution without high cost. Notes for each phase are listed in [`tests/`](tests/).

| Phase | Goal | Status |
|---|---|---|
| 1 | Test a single display at 60 FPS and measure power | ⬜ Not started |
| 2 | Prove the RS485 link with a disposable test sender | ⬜ Not started |
| 3 | Prove the state protocol and slave firmware on breadboards | ⬜ Not started |
| 4 | Confirm the display hardware | ⬜ Not started |
| 5 | Build small prototype | ⬜ Not started |
| 6 | Scale to 24, then 72 nodes | ⬜ Not started |
| 7 | Add applications and the web UI | ⬜ Not started |

<details>
<summary><strong>1. Test a single display at 60 FPS and measure power</strong></summary>

**1.1 Display performance (breadboard)**
- One GC9A01 dev board driven by a Pico
- Aim for 60 FPS

**1.2 Power measurement (run during 1a)**
- Measure how much power a single display and Pico consume

</details>

<details>
<summary><strong>2. Prove the RS485 link with a disposable test sender</strong></summary>

A slave can't be tested without something transmitting packets, so a sender has to exist this early, but it doesn't need to be the real master. A minimal, throwaway sender (a second Pico, or even a PC-side script) is enough to put bytes on the bus.

**2.1 RS485 link (breadboard)**
- Two Picos + two cheap RS485 breakout modules (MAX3485 / THVD1450).
- One Pico runs a minimal, disposable packet sender the other renders them as a slave.
- The packet should contain very basic instructions so the slave can draw something

</details>

<details>
<summary><strong>3. Prove the state protocol and slave firmware on breadboards</strong></summary>

Develop and test the state protocol and rendering pipeline on breadboards before committing to custom PCBs. Use a Pico as a sender.

**3.1 Protocol & slave firmware bring-up**
- Define and implement the state protocol: time display, basic drawing instructions, interface to implement widgets later
- Build the slave display-list executor: the firmware loop that parses commands, calls into a graphics library (TFT_eSPI or LovyanGFX) and displays pictures
- Test sync accross multiple nodes on 2–3 Picos: one sender, two slaves rendering the same broadcast
- Addressing here is manual, daisy-chain auto-addressing comes later

</details>

<details>
<summary><strong>4. Confirm the display hardware</strong></summary>

**4.1 Display hardware samples**
- Order a handful of the chosen GC9A01 breakout displays before finalizing any PCB.
- Confirm the breakout's pinout/pin order and its mounting-hole pattern / board outline, so the node board's mating board-to-board header position and any standoffs line up

</details>

<details>
<summary><strong>5. Build small prototype</strong></summary>

Before any further testing we should design the PCB as best as we can. Since the minimum order on JLCPCB is 5 pieces, 4 of them should be used to build a 2x2 prototype.

**5.1 PCB Prototype design**
- Design PCB
- Order a small batch (5 Pcs.) and bring up one board: power rails, USB enumeration, flashing, display, RS485 loopback.
- Test if the board works fine: Try to send data packets via RS485 using the sender from Phase 1b
- Measure the power consumption of this board

**5.2 2x2 prototype**
- Add daisy-chain auto-addressing in firmware, now that multiple nodes make it meaningful.
- Create a first version of the master controller in C#
- Assemble prototype and run tests
- Watch for voltage drops along the chain and try the mid-chain injection point to see how much it helps.

</details>

<details>
<summary><strong>6. Scale to 24, then 72 nodes</strong></summary>

**6.1 Scale**
- With real power numbers and a working board/bus, scale up.
- Power distribution will probably need its own pass at this size (multiple supplies, injection points, trace/connector currents).
- Adjust PCB design if anything arises

</details>

<details>
<summary><strong>7. Add applications and the web UI</strong></summary>

**7.1 Applications & web UI**
- Patterns, Snake, widgets. Once the transport feels solid, this part is mostly open-ended.

</details>

---

## ❓ Open questions

- **Power at scale.** See node_pcb's
- **Broadcast protocol definition.** Exact protocol definition. Needs to be implemented in `master` and `slave`.
- **Addressing scheme (firmware).** Daisy-chain auto-addressing to be *implemented* in firmware.
- **Syncing** How to make sure all displays run in sync

---

