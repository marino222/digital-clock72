# OPEN_QUESTIONS.md — Open Questions & Pre-Production Checklist

Consolidated from [`PROJECT_OVERVIEW.md`](PROJECT_OVERVIEW.md) §9 (project level) and
[`PCB_ARCHITECTURE.md`](PCB_ARCHITECTURE.md) §12 (hardware). Update this file as items are
resolved — and mirror the resolution back into the relevant canonical doc and
[`DECISIONS.md`](DECISIONS.md).

---

## Project-level open questions

- **Final scope / scale commitment.** Pilot → 24 → 72 is confirmed as the path; the full 72
  is **gated on pilot results and assembly capacity**.
- **Power at scale.** The dominant unknown. Resolved by *early measurement* (Phase 1a) — see
  the hardware checklist below. Every power figure in the docs is a placeholder until then.
- **Broadcast protocol definition.** Exact packet format and per-mode state schema (clock,
  pattern, Snake) — to be designed alongside `Master_Engine` and `Slave_Firmware`.
- **Addressing scheme.** Daisy-chain auto-addressing chosen over DIP switches; to be
  *implemented* in firmware.
- **Time-sync precision.** How tightly nodes must align for visually seamless animation
  (drives the broadcast/sync design).

---

## Hardware checklist — open items before production

- [ ] Confirm **GC9A01 FPC tail spec** (pin count / pitch / contact orientation) from
      *physical samples* — the single most common mistake on this board type.
- [ ] **Measure real per-node power** (idle / full white / inrush) in Phase 1a; replace all
      placeholder numbers.
- [ ] Verify the **local rail split**: backlight on raw 5 V, logic-only through the 3.3 V LDO.
- [ ] Choose the **exact transceiver P/N** and confirm LCSC stock (THVD1450 vs SN65HVD75).
- [ ] Finalize the **bus connector** (pin count, current rating, polarization, strain relief).
- [ ] **Power-distribution plan for 72 nodes** (injection points, trace/connector current).
- [ ] Pull a **live JLCPCB quote** with the finalized BOM.
- [ ] Decide **baud rate vs. max bus length** for the largest planned installation.

---

## Cross-references

- Bring-up phases that resolve several of these → [`ROADMAP.md`](ROADMAP.md).
- Rationale behind the already-made choices → [`DECISIONS.md`](DECISIONS.md).
