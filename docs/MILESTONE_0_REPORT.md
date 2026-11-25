# Milestone 0 report: foundation vertical slice

Date: 2026-11-24

## Features implemented

- Clean NEXUS monorepo boundaries for firmware, host, backend, dashboard, protocol, simulator, hardware, infrastructure, and documentation.
- Strict protocol 1.0 envelope validation and four-byte big-endian stream framing.
- Session hello/welcome negotiation, session sequencing, unsupported-message rejection, and reconnect-ready simulator behavior.
- Real local host telemetry for CPU load, memory, disk, battery, operating system, hostname, and Git repository state.
- 480×320 dark industrial simulator dashboard driven by protocol messages.
- ES3C40P board support constants and capability descriptor.
- ESP32-S3 native USB Serial/JTAG framed transport with bounded receive buffers and serialized writes.
- Native macOS AppKit menu-bar shell scaffold.
- Deterministic mock scenario and automated foundation tests.

## Architecture decisions

- The host is authoritative for normalized state; the device keeps a bounded read model.
- JSON semantics are independent of USB/TCP/WebSocket transport.
- TCP development mode is loopback-only and has no arbitrary command execution.
- The portable host core has no third-party Python dependency.
- Display/touch implementation is gated by physical revision and manufacturer initialization verification.
- The USB-C hub remains electrically and logically separate from ESP32-S3 firmware.

## Tests executed

| Check | Result |
| --- | --- |
| Python protocol/host tests | 9 passed |
| Python simulator tests | 2 passed |
| Host → framed TCP → simulator live telemetry | Passed; online protocol 1.0 session and live macOS/Git data observed |
| Swift 6 AppKit compile | Passed without source warning using direct `swiftc`; SwiftPM package also built before the final warning-only source correction |
| ESP-IDF 6.0.2 ESP32-S3 build | Passed |
| Firmware size | `0x2ce30` bytes; 82% of default 1 MiB app partition free |
| Physical ESP32-S3 flash | Passed; bootloader, partition table, and application hashes verified by `esptool` |
| Native USB protocol | Passed; welcome/ping received by device, correlated pong returned, and consecutive status frames decoded |

ESP-IDF emitted upstream Kconfig-format notes for NimBLE/FatFS and a non-fatal gdbinit-generation warning. No NEXUS source compile warnings or link failures remain.

## Verified functionality

- Frame fragmentation and coalescing.
- Malformed, duplicate-key, oversized, unknown-field, and pre-handshake rejection.
- Loopback handshake and telemetry snapshot publication.
- Simulator state reduction and live browser state endpoint.
- macOS telemetry acquisition without credentials or third-party packages.
- Firmware compile/link for ESP32-S3 N16R8 settings.
- Physical ESP32-S3 QFN56 revision 0.2, 16 MB flash, and 8 MB PSRAM identification.
- Native USB Serial/JTAG bidirectional framing on the physical Type-C connection.
- Stable five-second status cadence with 8,728,580 free heap bytes and no resets during validation.
- Swift/AppKit source compile.

## Unverified functionality

- Physical PCB revision.
- ST7796S initialization, landscape transform, and color order.
- FT6336U touch transform and interrupts.
- Audio codec, microphone, speaker, microSD, RGB, and battery ADC runtime behavior.
- Secure device pairing and macOS Keychain storage.
- Signed/packaged background host and menu-bar application.

## Known issues and limitations

- Development sessions use loopback-only unauthenticated mode; this must never be exposed on LAN.
- Firmware uses an epoch timestamp until host time synchronization is implemented.
- Firmware performs narrow field extraction during bring-up rather than full JSON schema validation.
- Simulator polling is suitable for local development, not the future configuration dashboard.
- Swift menu-bar status is scaffolded and does not yet reflect live device state.
- CPU percentage is derived from one-minute normalized system load, not sampled scheduler utilization.

## Run commands

```sh
# Terminal 1
./scripts/run_host.sh

# Terminal 2
./scripts/run_simulator.sh

# Open http://127.0.0.1:8080

./scripts/test.sh
./scripts/build_macos.sh

# After loading ESP-IDF 6.x
./scripts/build_firmware.sh
```

Physical flashing remains an intentional operator action. The following workflow was validated on the owned ES3C40P:

```sh
cd firmware
idf.py -p /dev/cu.usbmodemXXXX flash
```

Resolve the actual device path first; do not copy the placeholder port literally.

## Next highest-priority milestone

Record the owned PCB revision, audit the manufacturer example package, bring up ST7796S/FT6336U with LVGL on hardware, and implement the production macOS serial adapter for the validated framed USB protocol.
