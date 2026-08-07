# Phase 1 Hardware Validation

Phase 1 has two distinct statuses:

- **Implemented:** the firmware behavior exists and compiles.
- **Verified:** the behavior has passed this checklist on the physical watch.

Do not mark Phase 1 complete based on a successful build alone.

## Build under test

- Firmware version: `0.1.0`
- ESP-IDF: `6.0.2`
- Board: Waveshare ESP32-S3-Touch-AMOLED-2.06
- Date tested:
- Tester:
- Git commit or source archive identifier:

## Bring-up

- [ ] Erase flash, flash firmware, and capture the complete first-boot log.
- [ ] Confirm the log reports `AURA OS 0.1.0 booting` and `boot complete`.
- [ ] Confirm there are no watchdog resets, aborts, or repeated boot loops.
- [ ] Confirm the 410x502 display orientation, colors, and touch coordinates.
- [ ] Confirm PSRAM is detected and the configured 32 MB flash size is correct.
- [ ] Record all devices found on the shared I2C bus.
- [ ] Confirm PCF85063 at `0x51`.
- [ ] Confirm QMI8658 address (`0x6A` or `0x6B`) and update configuration.
- [ ] Confirm AXP2101 at `0x34` and identify its IRQ route from the schematic.

## Time and alarms

- [ ] Set the RTC, reboot without network access, and confirm time restoration.
- [ ] Power-cycle after at least one hour and record RTC drift.
- [ ] Verify invalid/unset RTC state logs a warning without crashing the UI.
- [ ] Verify a timer fires while the display is active.
- [ ] Verify a timer fires after the display has entered light sleep.
- [ ] Verify an alarm fires at the expected minute while active.
- [ ] Verify an alarm fires after entering light sleep.
- [ ] Verify the same enabled alarm fires again on the following day.
- [ ] Verify two alarms scheduled for the same minute both produce events.
- [ ] Verify Dismiss releases the alarm wake lock and normal timeout resumes.

## Power and input

- [ ] Verify ACTIVE -> DIM at 20 seconds and DIM -> SLEEP at 30 seconds.
- [ ] Leave the watch asleep for ten minutes; confirm the one-second time event
      does not illuminate or resume the display.
- [ ] Perform 100 sleep/wake cycles without a crash or stuck display.
- [ ] Verify a single touch interrupt returns to sleep.
- [ ] Tune and verify double-tap wake in hand and while worn.
- [ ] Verify BOOT wakes immediately from sleep.
- [ ] Verify BOOT short press returns to the face.
- [ ] Verify BOOT long press returns home.
- [ ] Verify touches on child controls, sliders, and tiles all reset inactivity.
- [ ] Open the flashlight while an alarm wake lock is held; closing either one
      must not release the other wake lock.
- [ ] Verify `Sleep now` enters sleep within one power-task interval.

## UI and persistence

- [ ] Set Capture mode, reboot, and confirm Quick Settings and the face agree.
- [ ] Toggle Capture/Silent and confirm the face updates immediately.
- [ ] Verify stopwatch start, pause, resume, and reset.
- [ ] Verify timer adjustment clamps to 1-120 minutes.
- [ ] Create and toggle alarms, reboot, and confirm NVS persistence.
- [ ] Verify flashlight restores configured active brightness when closed.
- [ ] Exercise every swipe direction and every placeholder tile.

## Battery and stability

- [ ] Replace the placeholder battery value with verified AXP2101 readings.
- [ ] Confirm charge state and percentage while unplugged, charging, and full.
- [ ] Record an eight-hour idle test: start %, end %, wake count, reset count.
- [ ] Record active-display drain for 30 minutes at 85% brightness.
- [ ] Run a minimum eight-hour soak with serial logging enabled.
- [ ] Record minimum free heap and task stack high-water marks.
- [ ] Confirm no watchdog, brownout, heap, or event-queue failures.

## Release gate

Phase 1 is complete only when all checks above pass or each exception has a
written issue, owner, and accepted workaround. Preserve the boot/soak logs and
battery measurements beside the release notes.
