# Node_PCB


## The broad idea

Each pixel node is one self contained board carrying one microcontroller and one round LCD, wired to its neighbors over a shared bus. The full array is this same board built N times, with a target of 72 nodes.

## What the board should contain

- An RP2040 microcontroller and an external flash chip
- A connector for the GC9A01 round LCD
- An RS485 transceiver and two bus connectors so boards can be daisy chained
- Power regulation, with separate rails for the logic and the display backlight
- USB-C and/or SWD pads for flashing firmware

## Manufacturing plan

The board is meant for full JLCPCB turnkey SMT assembly, with no hand soldering. Since the install needs dozens of identical boards, every part needs to be one JLC can place automatically.

## Design reference

Board layout will follow Raspberry Pi's RP2040 [hardware design guidelines](/docs/datasheets/hardware-design-with-rp2040.pdf).

## Block diagram (rough)

```
        ┌─────────────┐
        │   RP2040    │
        └──┬───┬───┬──┘
           │   │   │
     ┌─────▼┐ ┌▼───▼┐ ┌────────┐
     │ LCD  │ │Power│ │ RS485  │
     └──────┘ └─────┘ └───┬────┘
                       BUS IN/OUT
```

This is a placeholder. A cleaner diagram (image or PowerPoint export) can replace it later.

## Cost estimate

> Estimates, not a quote. JLCPCB fab/assembly fees and part prices shift over time. Pull a live quote with the finalized BOM before committing. Display price dominates and varies most by supplier.

### Per node bill of materials (electronics)

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
| 2 bus connectors | $0.20 |
| FPC connector | $0.15 |
| Passives (CC resistors, decoupling, bias, term, ~25 parts) | $0.25 |
| **MCU/board electronics subtotal** | **≈ $2.35** |
| GC9A01 1.28" FPC display | $3.00 to $4.00 |
| Bare PCB (2 layer, at volume) | $0.30 to $0.50 |
| SMT assembly (per board share) | $0.20 to $0.40 |
| **Estimated total per node** | **≈ $6.00 to $7.50** |


## Open uncertainties

- The GC9A01 FPC tail spec (pin count, pitch, orientation) needs to be confirmed from physical samples.
- Depending on availability of the displays, we might switch to dev boards instead of the bare display (prices might be similar)
- Real per node power draw (idle, full white, inrush) hasn't been measured yet.
- Exact RS485 transceiver part is not finalized (THVD1450 vs SN65HVD75).
- Bus connector choice (pin count, current rating, polarization) is still open.
- Baud rate vs. max bus length for the largest planned install needs to be decided.

See the root [README.md](../README.md) for full system context.
