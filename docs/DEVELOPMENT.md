# Development

## Prerequisites

- Python 3.11 or newer.
- Xcode/Swift for the menu-bar shell.
- ESP-IDF 6.x with the ESP32-S3 toolchain for firmware. The first build resolves the pinned `lvgl/lvgl` 8.4.0 component from the Espressif registry.

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

Do not flash until the connected serial port and owned board have been identified. Firmware 0.2.0 initializes ST7796S/LVGL and FT6336U after the native USB transport; failed optional hardware initialization is reported without disabling USB diagnostics.

## Tests

```sh
./scripts/test.sh
```

Tests are deterministic and do not need credentials or network access.
