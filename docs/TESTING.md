# Testing

## Foundation coverage

- Envelope validation and stream fragmentation/coalescing.
- Oversized and malformed frame rejection.
- Host telemetry shape and bounded values.
- Simulator state reduction.
- End-to-end loopback handshake and telemetry snapshot.
- ESP-IDF compile/link for ESP32-S3.
- Physical ESP32-S3 flash with hash verification.
- Bidirectional USB welcome/ping/pong and consecutive status-frame validation.
- ST7796S initialization at 40 MHz SPI with LVGL UI task ready.
- FT6336U I²C identity (`0x64`) and live press-coordinate validation.
- Swift menu-bar shell build.

## Validation labels

- **unit-tested** — deterministic automated test.
- **simulator-tested** — host-to-simulator workflow exercised.
- **build-verified** — target compiles/links only.
- **hardware-validated** — executed on the documented physical revision with evidence.

Flashing, native USB, and FT6336U input delivery are hardware-validated. The ST7796S/LVGL runtime is active, but visual color/orientation signoff is still required. Audio, storage, and auxiliary I/O are not validated.
