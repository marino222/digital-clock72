# Master_Engine

> **Status: Conceptual.** Nothing implemented yet — this folder reserves the home for the
> master "brain" software.

The **master** ("the brain") of the array. One instance runs on a **Raspberry Pi 3**.

- **Tech:** C# / **.NET 8**, **Dockerized**, headless Linux.
- **Owns everything global:** wall-clock time and synchronization, the vector math that
  decides what the whole array is doing, game state, and a **web UI** for control and config.
- **Broadcasts tiny global-state packets at 60 Hz** over the RS485 bus, it does **not**
  stream pixels (see [How it works](../README.md#how-it-works)).
- **Appliance resilience:** runs on a **read-only filesystem (OverlayFS)** so random power
  cuts don't corrupt the SD card.

## To be designed here

- The **broadcast protocol** — exact packet format and per-mode state schema (clock, pattern,
  Snake). See [Open questions](../README.md#open-questions) in the root README.
- **Time-sync** mechanism and required precision.
- The **web UI** for mode/config control.
- One real mode end-to-end first (clock), per the [Roadmap](../README.md#roadmap) step 4.

See the root [`README.md`](../README.md) for full system context.
