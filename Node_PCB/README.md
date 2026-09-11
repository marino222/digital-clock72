# Node_PCB


Each pixel node is one self contained board carrying one microcontroller and one round LCD, wired to its neighbors over a shared bus. The full array is this same board built N times, with a target of 72 nodes. This README describes one of the nodes.

## What the board contains

- An RP2040 microcontroller and an external flash chip
- A connector for the GC9A01 round LCD
- An RS485 transceiver and two bus connectors so boards can be daisy chained
- Power regulation, with separate rails for the logic and the display backlight
- USB-C and/or SWD pads for flashing firmware


## Design reference

The board layout follows Raspberry Pi's RP2040 [hardware design guidelines](/docs/datasheets/hardware-design-with-rp2040.pdf), which include a minimal RP2040 example design. Its KiCad files are open source, so they are used as the starting reference.

![RP2040 minimal design example](/docs/images/rp2040_minimal_design_example.png)

The image above shows this minimal design. Core components like the crystal oscillator, flash chip, and voltage regulator should stay the same. From there:

- Remove unnecessary I/O pins
- Add a 12-pin ZIF connector for the display
- Add the components needed for RS485 communication
- Add connectors for the power supply (power is not longer supplied via USB)
- Replace the Bootsel jumper pins (J2) with a button

Only SMD components are used, so the boards can be assembled by machine.


## Block diagram 

![Block diagram of PCB components](/docs/images/pcb_block_diagram.png)

## List of components

> All components need to be available in the JLCPCB parts library so they can be used for automatic assembly. Should they not be available, a suitable alternative has to be found.

Common parts like resistors and capacitors are not shown in this list. They may be viewed on the BOM of the final PCB design.

| Part | Manufacturer number | JLCPCB part number |  used in reference design |
| --- | --- | --- | --- | 
| MCU | RP2040 | C2040 | ✅ |
| Flash, 4 MB | W25Q32JVS | C97521 | ✅ |
| Crystal, 12 MHz | ABM8-272-T3 | C20625731 | ✅ |
| USB-C connector | TYPE-C-31-M-12 | C165948 | ❌ |
| BOOTSEL button | TS-1187A-B-A-B | C318884 | ❌ |
| RS485 transceiver | THVD1450DR | C2671361 | ❌ |
| ZIF, bottom contact | AFC07-S12FCC-00 | C11051 | ❌ |

> Note that the listed flash chip isn't the same as in the reference design. There a 16 Mb chip is used, which is very likely overkill for this project. To save costs this is scaled down to a 4 Mb chip. For this purpose the [hardware design guidelines](/docs/datasheets/hardware-design-with-rp2040.pdf) explicitly states that most 25-series flash devices may be used. So by using a smaller capacity storage from the same manufacturer shouldn't cause any trouble.

## Open uncertainties

- The GC9A01 FPC tail spec (pin count, pitch, orientation) needs to be confirmed from physical samples.
- Real per node power draw (idle, full white, inrush) hasn't been measured yet.
- Exact RS485 transceiver part is not finalized (THVD1450 vs SN65HVD75).
- Bus connector choice (pin count, current rating, polarization) is still open.
- Baud rate vs. max bus length for the largest planned install needs to be decided.

See the root [README.md](../README.md) for full system context.
