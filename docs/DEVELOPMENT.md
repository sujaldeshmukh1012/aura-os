# Development

## Prerequisites

- Python 3.11 or newer.
- Xcode/Swift for the menu-bar shell.
- ESP-IDF 6.x with the ESP32-S3 toolchain for firmware.

No Python packages are required for the Phase 0 vertical slice.

## Run

```sh
./scripts/run_host.sh
./scripts/run_simulator.sh
```

The host protocol listens at `127.0.0.1:8765`. The simulator UI listens at `127.0.0.1:8080`.

## Firmware

```sh
./scripts/build_firmware.sh
cd firmware
idf.py -p /dev/cu.usbmodemXXXX flash
```

Do not flash until the connected serial port and owned board have been identified. The current firmware validates board capabilities and native USB framing; display/touch code remains gated by the hardware checklist.

## Tests

```sh
./scripts/test.sh
```

Tests are deterministic and do not need credentials or network access.
