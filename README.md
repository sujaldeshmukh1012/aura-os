# AURA OS

From-scratch personal-assistant firmware for the Waveshare
ESP32-S3-Touch-AMOLED-2.06 watch. Thin, fast watch OS + companion "brain"
server. See docs/ARCHITECTURE.md and docs/ROADMAP.md.

## Build

The dependency lock currently resolves against ESP-IDF 6.0.2. Load that ESP-IDF
environment before building:

    . "$IDF_PATH/export.sh"
    idf.py set-target esp32s3       # first build only
    idf.py build
    idf.py -p /dev/tty.usbmodem101 flash monitor

Run host-side tests for pure clock/alarm logic with:

    sh tests/run_host_tests.sh

First build pulls the official Waveshare BSP + LVGL 9.5 from the component
registry automatically (main/idf_component.yml).

The build embeds firmware version `0.4.0-dev` and prints it during boot. Update
`PROJECT_VER` in the root `CMakeLists.txt` for releases.

## Current status

Phases 1 and 2 passed on physical hardware. Phase 3 and 4 implementation now
includes the Oracle-hosted Aura Server, calendar adapters, offline digest sync,
ANCS notifications, five-minute microSD audio pre-roll, reliable batch upload,
transcription/extraction, and approval-driven Memory UI. Run the combined
physical checklist in `docs/PHASE3_4_VALIDATION.md` before declaring both phases
hardware-complete.
