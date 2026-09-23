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
| RS485 transceiver | THVD1450DR | C2671361  | ❌ |
| ZIF, vertical | AFC11-S12ICA-00 | C262499 | ❌ |
| Schottky diode | B5819W SL | C8598 | ❌ |
| JST connector, 8-pin | BM08B-GHS-TBT(LF)(SN) | C133062 | ❌ |


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
        <td><b>Before</b></td>
        <td><b>After</b></td>
    <tr>
        <td><img src="/docs/images/minimal-example-power.png" alt="Minimal design example power block" /></td>
        <td><img src="/docs/images/schematic-power.png" alt="schematic power block" /></td>
    </tr>
</table>

The image on the left shows the power block of the minimal example, the image on the right shows our version. What changed:

- The Micro USB connector is replaced by a USB-C connector (J1). Its shield pins are tied directly to ground. The two D+ pins (A6/B6) are tied together, and so are the two D- pins (A7/B7), so the port works in either plug orientation. Both pairs then run to the MCU through 33 Ω series resistors.
- CC1 and CC2 are each pulled down with a 5.1 kΩ resistor. This is what tells the host that our board is a sink, so it enables +5 V on VBUS.
- The regulator stays as it is in the reference design: U1 (NCP1117) turns the +5 V input into the +3.3 V logic rail.

**Two power sources, two diodes**

During normal operation the board is not powered over USB — that port is only for development and testing. Power comes in over a JST connector, which also carries the RS485 signal lines. Each node has two of these connectors, so a board can be passed through to the next one in a daisy chain.

That means both sources can be present at the same time, so two Schottky diodes keep them apart:

- **D1** sits between the USB-C VBUS pin and the board's internal +5 V node. It lets the USB port feed the board, but blocks current from flowing back into the USB connector.
- **D2** sits between the bus (JST) +5 V net and that same internal node. It lets the bus feed the board, but stops the USB port from feeding the other boards on the bus.

If both sources are active, the board is simply supplied from the one with the higher potential. The price for this protection is a voltage drop across the diode, which is what the rest of this section is about.

#### How much input voltage does one board need?

Figure 1 shows a simplified version of our power supply circuit. The standard case is that the board is supplied from the VBUS line, so the path is JST → D2 → LDO.

<table>
    <tr>
        <td><img src="/docs/images/psu-simplified-circuit.png" alt="Simplified power path of one node: VBUS and USB-C each feed the LDO through their own Schottky diode" height="230" /></td>
        <td><img src="/docs/images/schottky-diode-voltage-drop.png" alt="Forward current over forward voltage of the B5819W Schottky diode" height="230" /></td>
    </tr>
    <tr>
        <td><b>Figure 1:</b> Simplified power path of one node. Both inputs reach the LDO through their own Schottky diode, so whichever drives the input, one diode drop is always in the way.</td>
        <td><b>Figure 2:</b> Forward characteristics of the B5819W. At I_F = 100 mA (red dot) the forward voltage is roughly 0.3 V.</td>
    </tr>
</table>

In our [tests](/tests/README.md), a single Raspberry Pi Pico plus a display and RS485 module drew around 60 mA. We round this up generously to **100 mA per board**.

Two parts in that path define the minimum input voltage, both taken at 100 mA:

| Part | Value at 100 mA | Source |
| --- | --- | --- |
| Schottky diode (B5819W) | V_F ≈ 0.3 V | [datasheet](/docs/datasheets/schottky-diode-B5817W-589W.pdf), see Figure 2 |
| NCP1117 LDO | V_in - V_out = 1.10 V (max) | [datasheet](/docs/datasheets/NCP1117-LDO.PDF), 0.95 V typical / 1.10 V max |

The 1.10 V is the LDO's *dropout voltage*: the **minimum** difference the regulator needs between its input and its output. If V_in - V_out falls below it, the LDO drops out of regulation and the 3.3 V rail starts to sag. With V_out = 3.3 V that gives:

```
V_in(LDO) >= V_out + V_dropout = 3.3 V + 1.10 V = 4.40 V
V_bus     >= V_in(LDO) + V_F   = 4.40 V + 0.3 V = 4.70 V
```

So the +5 V on the bus connector must never drop below **4.7 V**, anywhere in the installation.

#### How much is left at the last board?

Since we're not living in an ideal world, the supply voltage won't be exactly 5.0 V. We assume the boards are arranged in 12 rows of 6, with a power injection point at the start of each row, as shown in the diagram below. Each row then draws around 6 × 100 mA = 600 mA, which the JST connectors handle comfortably (the GH series is rated for 1.0 A). Most power supply manufacturers specify a voltage tolerance of ±5 %, so in the worst case the PSU delivers 4.75 V instead of 5.00 V.

![Wiring diagram](/docs/images/wiring-diagram.png)

The worst case is node a6: it is the last board of a row and therefore the furthest away from the PSU. Between it and the injection point sit 5 boards and the wires between them, and each of them adds resistance (contacts, pcb traces, wires).

**Estimated resistance per board**

The [datasheet](/docs/datasheets/BM08B-GHS-TBT(LF)(SN).pdf) specifies a contact resistance of 30 mΩ.


Together with the PCB traces and the wire to the next board, we estimate roughly **100 mΩ per hop** for the whole current loop (supply and ground return). The table below shows the resulting voltage drops, with U_n and I_n as labelled in the diagram above:

| Hop | Current | Voltage drop (U = R × I) |
| --- | --- | --- |
| U1 | I1 = 600 mA | 60 mV |
| U2 | I2 = 500 mA | 50 mV |
| U3 | I3 = 400 mA | 40 mV |
| U4 | I4 = 300 mA | 30 mV |
| U5 | I5 = 200 mA | 20 mV |
| **Total** | | **200 mV** |


```
4.75 V (PSU worst case) - 0.20 V (wiring) = 4.55 V at node a6
```

That is 0.15 V short of the 4.7 V we calculated as the minimum input voltage.

In reality this will probably still be fine, because every assumption above is a worst case. The PSU at the bottom of its tolerance, 100 mA per board when we measured 60 mA, and the full current charged to every hop. On the other hand, the 0.3 V diode drop is read off a typical curve and not a guaranteed maximum, so it could also turn out slightly worse.

Either way, the 100 mΩ per hop is an estimate. Once the first batch of PCBs arrives we have to measure the actual resistance and the voltage that really arrives at the last node (see [tests](../tests/README.md#pcb-prototype-design) for the measured results).


### Flash storage

<table>
    <tr>
        <td><b>Before</b></td>
        <td><b>After</b></td>
    </tr>
    <tr>
        <td><img src="/docs/images/minimal-example-flash.png" alt="Minimal design example flash block" /></td>
        <td><img src="/docs/images/schematic-flash.png" alt="schematic flash block" /></td>
    </tr>
</table>

The only changes here are replacing the BOOTSEL jumper J2 with a button (SW1) for easier entry into bootselect mode, and swapping in a different flash chip that shares the exact same footprint as the one in the reference design.

### RS485 communication

![Schematic RS485](/docs/images/schematic-rs485.png)

**Why this transceiver.** The THVD1450DR is a 1/8 unit-load part, so up to 256 nodes can share one bus segment. Since the array targets 72 nodes, that leaves plenty of headroom. It is also a common, widely available part and is stocked by JLCPCB.

**What the circuit does.** U4 runs off +3V3 and connects the RP2040's UART to the differential pair:

- `GPIO12_TX` → `DI` and `RO` → `GPIO13_RX` carry the data.
- `GPIO15_DE` and `GPIO14_RE` control the direction. Both are pulled to GND through 10 kΩ (R6, R9), so on power-up the driver is disabled and the receiver is enabled.
- C18 (100 nF) sits directly at pin 8, with C19 (1 µF) immediately upstream on the same 3V3 feed.

This follows the layout guidelines in the chip's [datasheet](/docs/datasheets/thvd1450.pdf), which ask for a 100–220 nF cap as close as possible to VCC, at least two vias per decoupling connection, and 1–10 kΩ resistors on the enable lines to limit noise currents during transients. 

**Termination.** R12 (120 Ω) is populated on every board but sits in series with solder jumper JP1, so it is only in circuit once that jumper is bridged. That matters because of how the array is wired. Looking back at the [wiring diagram](/docs/images/wiring-diagram.png), this is not one continuous bus. The 72 nodes are split into 12 rows of 6, each fed from its own injection point, so electrically there are 12 separate branches. Each branch is a short run, and terminating all 24 ends is likely overkill. How many jumpers actually need bridging still has to be measured on real hardware.

> **Note on the part number.** The THVD1450 is the 50 Mbps variant of the family, the pin-compatible THVD1410 is the 500 kbps.

### Daisy chain connector

![Schematic daisy chain connector](/docs/images/schematic-connector.png)

Boards are linked with 8-pin JST GH connectors (BM08B-GHS-TBT), one in (J3) and one out (J2). Two things made this the pick:

- **Top entry.** The gap between adjacent boards is not confirmed yet. A vertical connector routes the cable away from the board edge, so the gap size does not constrain the choice. A side-entry part (SM08B-GHS-TB) would.
- **SMT.** The whole board is machine assembled, and this header is surface mount, so no hand soldering step is needed.

The eight lines are allocated as follows:

| Pins | Net | Notes |
| --- | --- | --- |
| 1, 4 | GND | Split across two pins |
| 2, 3 | +5V (VBUS) | Split across two pins |
| 5, 6 | RS485_A / RS485_B | Passed straight through |
| 7 | ADDR | Point to point, for auto-addressing |
| 8 | SPARE | Point to point, reserved |

Power and ground each get two pins on purpose. The GH series is rated for 1.0 A per contact with AWG #26 wire, so doubling up gives a comfortable 2 A budget.

The last two lines are wired differently from the rest. Power, ground and the RS485 pair passed straight through the board, `ADDR` and `SPARE` are not. `ADDR_IN`/`SPARE_IN` on J3 and `ADDR_OUT`/`SPARE_OUT` on J2 are four separate nets going to four separate GPIOs. `ADDR` is used for the auto adressing feature. `SPARE` is the same arrangement with nothing assigned to it yet, wired up proactively so a future feature can use it without a board change.

C20 (10 µF) provides local bulk decoupling between +5V and GND at the connector.

### SWD debug interface

![Schematic SWD debug](/docs/images/schematic-debug.png)

An SWD header (J4) was added alongside USB-C to make flashing and debugging easier. The footprint is a standard TC2030, which needs no connector on the board at all. A pogo pin cable is simply pressed against the pads.

On the other end, the plan is to drive it from a second Raspberry Pi Pico running the official debugprobe firmware, which handles both flashing and interactive debugging over the same three wires.

### SPI display

![Schematic SPI](/docs/images/schematic-spi.png)

The display's FPC tail plugs into J5, a 12-pin ZIF (zero insertion force) connector, so the panel can be swapped without soldering. The assignment follows the tail pinout given in the panel [datasheet](/docs/datasheets/HZ0128QVPHGWS01N-AA.pdf), which the two [GoldenMorning T128HC](/docs/datasheets/display-datasheets/) drawings of the same 12-pin tail agree with:

| Pin | Net | Purpose |
| --- | --- | --- |
| 1 | GND | |
| 2 | LEDK | Backlight cathode, switched by Q1 |
| 3 | LEDA | Backlight anode, +5V through R11 |
| 4 | +3V3 | VDD, analog supply |
| 5, 6 | GND | |
| 7 | D/C | Data / command select |
| 8 | CS | Chip select, active low |
| 9 | SPI0_SCK | Serial clock |
| 10 | SPI0_TX | Serial data in |
| 11 | RESET | Active low |
| 12 | GND | |
| 13, 14 | GND | Mounting tabs |

Ten of the twelve pins are unremarkable: the 4-wire SPI signals, `RESET`, the 3V3 analog supply and four grounds. Note that VDD is specified as 2.8 V typical with an absolute maximum of 4.6 V, so it belongs on the logic rail, the +5V rail is only for the backlight.

**The backlight.** This is the one block that needs more than a wire. The panel carries two white LEDs in parallel, specified at VF ≈ 3.0 V and IF = 40 mA, and it has no driver of its own.

`LEDA` (pin 3) is fed from +5V rather than the 3V3 rail, because 3.3 V leaves almost no headroom above the 3.0 V forward voltage. `LEDK` (pin 2) is then switched low-side by Q1, an S8050 NPN: `BL_PWM` drives the base through R13 (2.2 kΩ), and R14 (10 kΩ) pulls the base down so the backlight stays dark until the firmware deliberately drives the pin. Varying the PWM duty cycle dims the display.

R11 (47 Ω) sits in series with the anode and sets the current:

```
I = (5 V − 3.0 V − 0.2 V) / 47 Ω ≈ 38 mA
```

with the 0.2 V accounting for Q1's collector-emitter saturation voltage. That lands just under the 40 mA rating, which is where we want to be.


## Open uncertainties

- The GC9A01 FPC tail spec (pin count, pitch, orientation) needs to be confirmed from physical samples, and the ZIF pin assignment corrected to match (see the SPI section).
- Real per node power draw (idle, full white, inrush) hasn't been measured yet.
- The THVD1450 is in the schematic, but the slower, lower-EMI THVD1410 may be the better fit for this bus.
- How many of the 12 branches actually need their 120 Ω termination jumper bridged has to be measured.
- Baud rate vs. max bus length for the largest planned install needs to be decided.

See the root [README.md](../README.md) for full system context.
