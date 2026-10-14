# Mechanical Design

The project needs an enclosure, plus parts to hold the node PCBs in place.

The development of the mechanical design is underway. All 3D models are made with OnShape and are available under this [public link](https://cad.onshape.com/documents/53aa7c255b66f5562a1ca156/w/211967f8d6f687de7cb12e88/e/a9185ed7945d5b9016d4c432?renderMode=0&uiState=6acfdefbca73a9d6ca868085).

## Contents

- [Concept](#concept)
- [Node assembly](#node-assembly)
- [FPC tail routing](#fpc-tail-routing)
- [PCB outline](#pcb-outline)

## Concept

The clock is a 12 x 6 grid of round displays, aligned vertically (see Figure 1). A software change could tilt this alignment instead.

The display's [technical drawing](/docs/datasheets/HZ0128QVPHGWS01N-AA.pdf) gives an outer diameter of ≈36 mm. A pitch of 44 mm between displays gives a visually pleasing result. Consequently, each PCB must fit within **44 x 44 mm** (Figure 2).

<table>
  <tr>
    <td align="center" width="50%"><img src="/docs/images/mechanical-concept.jpg" alt="Concept sketch" height="380"></td>
    <td align="center" width="50%"><img src="/docs/images/pcb-outer-dimensions.png" alt="PCB outer dimensions" height="380"></td>
  </tr>
  <tr>
    <td align="center"><sub><b>Figure 1:</b> Concept sketch of the clock layout</sub></td>
    <td align="center"><sub><b>Figure 2:</b> PCB outer dimensions (44 x 44 mm)</sub></td>
  </tr>
</table>

## Node assembly

Figure 3 shows how a node is assembled. The display panel is stuck onto the back of the PCB, and its FPC connector tail wraps around the PCB's edge to reach the connector. A small cutout is needed for the tail so it doesn't interfere with neighboring PCBs.

The connector must be positioned so that the FPC contacts face the correct side. According to the display's [technical drawing](/docs/datasheets/HZ0128QVPHGWS01N-AA.pdf), the contact side faces away from the display (Figure 4).

<table>
  <tr>
    <td align="center" width="45%"><img src="/docs/images/pcb-stack-concept.jpg" alt="PCB stack concept" width="360"></td>
    <td align="center" width="55%"><img src="/docs/images/fpc-contact-side.png" alt="FPC contact side" width="420"></td>
  </tr>
  <tr>
    <td align="center"><sub><b>Figure 3:</b> Node assembly: display, PCB and FPC tail</sub></td>
    <td align="center"><sub><b>Figure 4:</b> The FPC contacts face away from the display</sub></td>
  </tr>
</table>

## FPC tail routing

The tail can be routed in many ways, and different suppliers of the same display use different tail lengths. The routing must therefore accommodate a range of lengths.

During [PCB design](/pcb/README.md#list-of-components) a **vertical-facing ZIF connector** was chosen. Space in X and Y is limited by neighboring PCBs, so the tail must not stick out to the side. With a vertical connector, excess tail length can be absorbed in a loop that expands in the Z direction.

This restricts the routing to the three options in Figure 5:

<p align="center">
  <img src="/docs/images/fpc-tail-routing-options.png" alt="FPC tail routing options" width="380"><br>
  <sub><b>Figure 5:</b> Three FPC tail routing options (side view)</sub>
</p>

| Option | Contacts face | Best for |
| ------ | ------------- | -------- |
| 1 | PCB center | longer tails |
| 2 | PCB center | longer tails |
| 3 | PCB edge | short tails |

### Decision

The connector orientation has to be fixed, so one option must be chosen. **Options 1 and 2 are used, with the contacts facing the center of the PCB.** This limits the usable tail lengths (very short tails will not reach), but a decision has to be made.

Because options 1 and 2 wrap the tail around opposite PCB edges (left or right), cutouts are needed on both sides.

## PCB outline

The resulting PCB outline, with a cutout on each side, is shown in Figure 6.

<p align="center">
  <img src="/docs/images/pcb-outline.png" alt="PCB outline" width="340"><br>
  <sub><b>Figure 6:</b> Suggested PCB outline with cutouts for the FPC tail</sub>
</p>
