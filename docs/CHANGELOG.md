# Changelog

## 0.2.1

- Rotated the ST7796S and FT6336U mappings together by 180 degrees for the board's upright landscape orientation.

## Unreleased

- Reframed Aura OS v2 as the NEXUS Personal Intelligence Dock.
- Added ES3C40P hardware research and explicit physical-verification gates.
- Added protocol 1.0 framing and typed Python implementation.
- Added real host telemetry, device simulator, firmware scaffold, and macOS menu-bar shell.
- Added foundation tests and persistent implementation tracking.
- Flashed the owned ES3C40P and hardware-validated stable bidirectional NEXUS framing over native USB.
- Added the native ST7796S SPI driver using LCDWiki's initialization sequence.
- Added LVGL 8.4 with a 480×320 NEXUS hardware-diagnostic UI.
- Added FT6336U I²C discovery, portrait-to-landscape coordinate conversion, and LVGL pointer input.
- Flashed firmware 0.2.0 and observed chip ID `0x64`, live touch events, stable UI readiness, and no resets.
