# Contributing

This is a personal hardware + software project in active design. Contributions, ideas, and
review are welcome — the notes below keep things tidy as the project grows.

## Where things live

| Area | Folder | Toolchain |
|---|---|---|
| Master "brain" | [`Master_Engine/`](Master_Engine/) | C# / .NET 8, Docker |
| Node firmware | [`Slave_Firmware/`](Slave_Firmware/) | C++ / PlatformIO (RP2040) |
| Node PCB | [`Node_PCB/`](Node_PCB/) | KiCad |
| Design docs | [`docs/`](docs/) | Markdown |

## Documentation is the source of truth

The design lives in [`docs/`](docs/). The two canonical documents are
[`PROJECT_OVERVIEW.md`](docs/PROJECT_OVERVIEW.md) (system & vision) and
[`PCB_ARCHITECTURE.md`](docs/PCB_ARCHITECTURE.md) (node hardware). When a design decision
changes:

1. Update the relevant canonical doc.
2. Reflect it in [`docs/DECISIONS.md`](docs/DECISIONS.md) (add/amend the log entry with the
   reason).
3. If it resolves or raises a question, update [`docs/OPEN_QUESTIONS.md`](docs/OPEN_QUESTIONS.md).
4. Update the summary tables in [`README.md`](README.md) if a top-level choice changed.

Keep the canonical docs authoritative and the README a summary that links to them — avoid
duplicating detail that will drift.

## Commits

- Use clear, present-tense commit subjects (e.g. `Add RS485 termination jumper to node PCB`).
- Group hardware, firmware, engine, and docs changes into separate commits where practical.

## Hardware safety / sanity gates

Before spending money on PCBs, respect the bring-up gates in
[`docs/ROADMAP.md`](docs/ROADMAP.md) — especially:

- Confirm the **GC9A01 FPC tail** (pin count / pitch / orientation) from *physical samples*
  before finalizing any layout.
- **Measure real per-node power** before sizing PSUs, connectors, and trace widths.
