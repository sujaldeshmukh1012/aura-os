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
- Swift menu-bar shell build.

## Validation labels

- **unit-tested** — deterministic automated test.
- **simulator-tested** — host-to-simulator workflow exercised.
- **build-verified** — target compiles/links only.
- **hardware-validated** — executed on the documented physical revision with evidence.

Phase 0 flashing and native USB runtime behavior are hardware-validated. Display, touch, audio, storage, and auxiliary I/O are not.
