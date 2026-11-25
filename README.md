# NEXUS Personal Intelligence Dock

NEXUS is a local-first physical control plane for projects, attention, engineering workflows, and AI agents. The target device is the LCDWiki ES3C40P 4-inch ESP32-S3 touchscreen, paired with a background macOS host service.

This branch contains the clean-sheet v2 implementation. Aura OS v1 remains available on `archive/aura-os-v1`.

## Current milestone

The Phase 0 vertical slice includes:

- A versioned, transport-neutral framed JSON protocol.
- A real macOS telemetry service using only the Python standard library.
- A 480×320 landscape browser simulator using the same protocol as firmware.
- An ESP-IDF firmware target with an ES3C40P board support package and native USB transport.
- A native macOS menu-bar launcher scaffold.
- Protocol, service, simulator, and firmware build tests.
- Architecture, hardware, security, roadmap, and implementation tracking documents.

Display and touch initialization are deliberately gated until the physical PCB revision and the manufacturer's ESP-IDF initialization sequence are checked against the owned board. Simulator-tested features are not labeled as hardware-validated.

## Run the vertical slice

Terminal 1:

```sh
./scripts/run_host.sh
```

Terminal 2:

```sh
./scripts/run_simulator.sh
```

Then open <http://127.0.0.1:8080>. The dashboard displays live host CPU load, memory, disk, battery, and repository state.

## Test

```sh
./scripts/test.sh
./scripts/build_firmware.sh
./scripts/build_macos.sh
```

## Repository map

- `firmware/` — ESP-IDF device firmware and board support.
- `host/` — portable host core and native macOS menu-bar shell.
- `backend/` — local-first intelligence backend boundary.
- `dashboard/` — future configuration application.
- `protocol/` — schemas, examples, and wire documentation.
- `simulator/` — deterministic device simulator and dashboard.
- `hardware/` — enclosure and expansion hardware records.
- `docs/` — architecture, security, development, and tracker.

Start with `docs/IMPLEMENTATION_TRACKER.md`; it is the persistent handoff between development sessions.
