# Node PCB


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
| Schottky diode | B5819W SL | C8598 | ❌ |


> Note that the listed flash chip isn't the same as in the reference design. There a 16 Mb chip is used, which is very likely overkill for this project. To save costs this is scaled down to a 4 Mb chip. For this purpose the [hardware design guidelines](/docs/datasheets/hardware-design-with-rp2040.pdf) explicitly states that most 25-series flash devices may be used. So by using a smaller capacity storage from the same manufacturer shouldn't cause any trouble.

---
---
# Designing the PCB

As a starting point, the KiCad files of the minimal example mentioned above we're copied. 

## Adjusting schematic

The first step is to adjust the [schematic](/docs/datasheets/rp2040-design-example-schematic.pdf) to our needs.

### Power supply

<table>
    <tr>
        <td><img src="/docs/images/minimal-example-power.png" alt="Minimal design example power block" /></td>
        <td><img src="/docs/images/schematic-power.png" alt="schematic power block" /></td>
    </tr>
</table>

The image on the left shows the power block of the minimal example, the image on the right shows our changes. The main change is replacing the Micro USB connector with a USB-C connector. Its shield pins are tied directly to ground, and the D+/D- data lines are tied together and routed to the MCU. The USB-C connector supplies +5V, which U1 converts to +3.3V. This part is unchanged from the reference design. The CC1 and CC2 configuration channels are each pulled down with a 5.1 kΩ resistor.

During normal operation, power is not supplied via USB. The USB port is only for development and testing. Instead, a JST connector supplies power and also carries the RS485 signal lines. Each node has two JST connectors, allowing the board to be passed through and connected to the next one in a daisy chain. This means a board could theoretically be powered from both sources at once, so a Schottky diode is placed in series with the USB-C connector's VBUS pin to protect the circuit while still allowing USB-C to be connected even when the board is already powered.

JST connectors in a daisy chain add a considerable amount of resistance, so we need to size them correctly using power calculations based on realistic values. In our [tests](/tests/README.md), a single Raspberry Pi Pico plus a display and RS485 module drew around 60 mA. We round this up generously to 100 mA per board.

We assume the boards are arranged in 12 rows of 6, with a power injection point at the start of each row, as shown in the diagram below. Each row should then draw around 600 mA. The first JST connector in each row needs to handle this current, which shouldn't be a problem since most connectors are rated above that. To keep some headroom, we plan to use a 6-pin connector: 2 pins for data, and 2 pins each for ground and supply voltage.

![Wiring diagram](/docs/images/wiring-diagram.png)

A separate issue is the voltage drop across each board. The supply voltage at the last board of a row will almost certainly be below 5V, since both the JST connectors and the boards themselves add resistance. With boards spaced roughly 40 mm apart and two JST connectors per board, we estimate the resistance per board at around 150 mΩ. This puts the voltage drop from the first to the last board in a row at around 315 mV, meaning the last board would be supplied with about 4.7 V. This matters most for the display's LED backlight, which runs directly off 5V rail. The voltage drop means each display will end up with a different brightness. We address this issue when designing the display connection.

## Open uncertainties

- The GC9A01 FPC tail spec (pin count, pitch, orientation) needs to be confirmed from physical samples.
- Real per node power draw (idle, full white, inrush) hasn't been measured yet.
- Exact RS485 transceiver part is not finalized (THVD1450 vs SN65HVD75).
- Bus connector choice (pin count, current rating, polarization) is still open.
- Baud rate vs. max bus length for the largest planned install needs to be decided.

See the root [README.md](../README.md) for full system context.
