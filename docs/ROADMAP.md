# ROADMAP.md — Milestones & Bring-up Plan

This file combines the **project-level milestones** (from
[`PROJECT_OVERVIEW.md`](PROJECT_OVERVIEW.md) §8) with the **phased hardware bring-up plan**
(from [`PCB_ARCHITECTURE.md`](PCB_ARCHITECTURE.md) §10) into one view.

**Guiding philosophy:** *prove the riskiest cheap thing first; spend money on PCBs only
after the unknowns are gone.* The pilot is deliberately a **3×3 (9-node)** array — large
enough to exercise the multi-node bus, daisy-chain addressing, sync, and power sharing, but
cheap and debuggable. Each phase has a clear pass/fail gate.

---

## Project-level milestones

1. **Prove a single display** at 60 FPS (Pico + dev board) and **measure power**.
2. **Prove the bus** — two MCUs over RS485, master→slave state broadcast.
3. **Prove one custom node board**, then a **3×3 pilot** as a full system test.
4. **Stand up the Master_Engine** (C#/.NET 8) with time sync + one real mode (clock).
5. **Implement the broadcast protocol + auto-addressing** end-to-end on the pilot.
6. **Scale** to 24, then 72, applying measured power and injection-point rules.
7. **Add applications** (patterns, Snake) and the web UI on top of the proven transport.

---

## Phased hardware bring-up

### Phase 1a — Display performance (breadboard)
- One GC9A01 **dev board** driven by a **Pico**.
- **Gate:** hit the real target — **60 FPS, DMA, double-buffered**. This validates the
  performance budget the whole architecture rests on. Find out here, not after 10 PCBs.
- **Also measure power here** — the cheapest place to get it.

### Phase 1b — RS485 link (breadboard)
- Two **Picos** + two cheap RS485 breakout modules (MAX3485 / THVD1450).
- **Gate:** master broadcasts state packets, slave renders locally; packet format and the
  60 Hz broadcast loop proven before anything is on a PCB.

### Phase 1-power — Measurement protocol (run during 1a)
Put a USB power meter or bench supply (with current readout) inline and record:
- **Idle / typical content** (clock face, sparse pixels)
- **Full white** (worst case: backlight + all pixels)
- **Inrush** at power-on (spike when displays light)

→ Replace all placeholder power numbers with **measured per-node value × N**. This turns PSU
sizing, connector ratings, and trace widths from guesses into facts.

### Phase 2 — FPC display samples
- Order **~10 GC9A01 FPC-tail displays** *before finalizing any PCB*.
- **Gate:** physically confirm tail **pin count / pitch / contact orientation** against the
  datasheet. The FPC connector footprint is the one thing on the board that can't be cloned
  from a reference — it's defined by the display that actually arrives.

### Phase 3 — Single-node board (the key insurance step)
- Order **~5 of a single-node board** and fully bring up **one**.
- **Why:** the QFN-56 RP2040 core + FPC connector are the only genuinely new/risky parts to
  solder. Prove one board powers up, enumerates, flashes, and drives a display before
  multiplying by 10. A footprint bug is then a ~$30 lesson, not a ~$150 one.
- **Bring-up order:** power rails (3.3 V, 1.1 V) → BOOTSEL/USB enumeration → flash blink →
  display init → 60 FPS DMA render → RS485 loopback.

### Phase 4 — 3×3 pilot (system test)
- Order **~10 boards** (9 nodes + spare). This is now a *system* test, not a board test.
- **Test:** daisy-chain bus, auto-addressing, 9-node sync, and **power as a deliberate
  experiment** — measure 5 V droop node 1 → node 9 under full white, and test the
  **mid-chain injection point** to see how it flattens droop.
- **Also prove the mechanical chain here:** the real inter-board connector, cable, strain
  relief, and polarization (so a board can't be plugged in backwards). Pick the production
  connector now — don't defer the harness to "later."
- **Pilot PSU:** ~9 × measured peak with margin (≈ 5 V / 5 A if peak ≈ 250 mA/node).

### Phase 5 — Scale
- With measured power numbers, a proven board, and a validated bus, scale to 24 → 72.
- Apply the injection-point rule learned in Phase 4; treat 72-node power distribution as its
  own design pass (multiple supplies, injection points, trace/connector currents).

---

## Refined sequence at a glance

```
1a display perf → 1b RS485 link (+ measure power) → 2 FPC samples → 3 single-node board → 4 3×3 pilot → 5 scale
```
