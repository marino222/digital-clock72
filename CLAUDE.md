# Working on this repo

## Writing style
Applies to all docs, READMEs, code comments and replies.
- Simple and technical at the same time. Easy to understand without losing precision.
- Short sentences. One idea per sentence.
- No filler, no marketing, no repeating what was already said.
- Use the exact term once it is defined, and keep using the same term.
- Prefer a table or a short list over a long paragraph.

## Workflow
- The user designs the architecture. Claude is a sparring partner: explain options,
  challenge decisions, review drafts. No firmware code until the user says the
  architecture part is settled.
- Everything written into architecture docs goes through the user first.

## Software
- Base principles: software/README.md. Follow them, don't restate them elsewhere.
- Firmware lives in software/master (ESP32-S3) and software/slave (RP2040).
  The shared protocol lives in software/shared.
- A finished phase is frozen with a git tag, not copied into tests/.
  tests/README.md names the tag and the build/flash commands.
- The master animates a virtual grid and maps a window of it onto the attached
  nodes. Grid size and window are runtime web UI settings, stored in NVS.
  The same master binary works on any hardware.
- Slaves only know their own address, never the total node count.
  One software/slave supports every PCB with the same pinout. If the pinout
  changes, tag the last commit that supports the old boards.
- Scope: clock and simple widgets. Never mention GIFs.

## Architecture docs (software/master, software/slave READMEs)
- Goal: a mental model readable in about 5 minutes, not a spec.
  Readability over completeness.
- Sections: Overview (with block diagram), File map, Data flow, Build and flash.
  States and timing only when there is something to say.
- Module level only. Function-level docs go in header comments.
- Diagrams: Mermaid, at most block, state and timeline.
- No decision log. Give a reason only where it stops someone from "fixing" it wrongly.
- An architecture change updates the README in the same commit.

## Code
- One .h/.cpp pair per own module. The header is the interface.
  Exceptions: config headers (LGFX_Config.hpp, pin definitions) and shared/protocol.h.

## Tests (tests/)
- Minimal test projects: one behaviour, envs `pico` (USB) and `pico_swd` (CMSIS-DAP).
- No extra modes or configurability nobody asked for. Offer them instead of adding them.
- No serial output when the test runs without a monitor.

## Where docs go
- System power (PSU, rows, wiring, brightness): power/README.md.
- Power path and backlight on one board: pcb/README.md.
- Raw measurements and test results: tests/README.md.
- Ideas for the next PCB revision: pcb/README.md, "Known issues and ideas for revision 2".
