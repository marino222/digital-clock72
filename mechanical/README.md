# Mechanical Design

The project needs an enclosure, plus parts to hold the node PCBs in place.

## Concept

The sketch below shows the rough concept of the clock's physical layout: a 12x6 grid of displays, aligned vertically. A software change could tilt this alignment instead.

The display's [technical drawing](/docs/datasheets/HZ0128QVPHGWS01N-AA.pdf) gives an outer diameter of ≈36 mm. Spacing the displays 44 mm apart gives a visually pleasing result.

![Concept sketch](/docs/images/mechanical-concept.jpg)

This spacing means each PCB must fit within 44 x 44 mm, as shown below.

![PCBs outer dimensions](/docs/images/pcb-outer-dimensions.png)

### PCB outline

The sketch below shows how a node is assembled. The display panel is stuck onto the back of the PCB, and its FPC connector tail wraps around the PCB's edge to reach the connector. A small cutout is needed for the tail so it doesn't interfere with neighboring PCBs. The connector's position will need to match the actual tail length.

![PCB stack concept](/docs/images/pcb-stack-concept.jpg)

This gives us the PCB outline shown below, which we need to follow.

![PCB outline](/docs/images/pcb-outline.png)