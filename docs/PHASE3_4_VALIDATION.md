# Phase 3 + 4 hardware validation

Use this after deploying the updated server and flashing firmware `0.4.0-dev`.

## Server and persistence

- [ ] `https://aura.sujalbhakare.com/health` returns `status: ok`.
- [ ] Mission Control opens over HTTPS and rejects incorrect credentials.
- [ ] An authenticated `/api/v1/digest` request returns events, tasks, and
      proposals.
- [ ] Reboot the Oracle instance; both containers become healthy without manual
      intervention and existing tasks remain present.
- [ ] Run `deploy/oracle/backup.sh`; verify the resulting SQLite file is nonzero.
- [ ] Run calendar sync. Each configured provider reports `ok` in System.

## Connected watch

- [ ] Calendar and Mission work while online.
- [ ] Disable Wi-Fi and confirm the last digest remains visible.
- [ ] Re-enable Wi-Fi and confirm changes sync within ten minutes.
- [ ] Pair Aura Watch in iPhone Bluetooth settings and accept the notification
      permission prompt.
- [ ] A new iPhone notification appears in the swipe-up panel.
- [ ] Dismiss removes an actionable notification on the iPhone.
- [ ] BLE reconnects after walking out of range and returning.

## AI Memory

- [ ] Insert a writable microSD card with one MBR/FAT32 partition. exFAT and
      APFS are not supported. Reboot after inserting or reformatting the card
      because storage is mounted during startup.
- [ ] Reboot and confirm the watch always begins in Silent with the microphone
      closed, even if it was previously powered off in another mode.
- [ ] From Silent, press PWR once. Confirm Stealth is shown and stays active
      through speech and silence. Press once again and confirm Silent.
- [ ] From Silent or Stealth, double-press PWR within 600 ms. Confirm Capture
      starts. Speak for ten seconds; it must not stop automatically on silence.
- [ ] While in Capture, press PWR once. Confirm it saves one WAV, returns to
      Stealth, and immediately begins a new rolling buffer.
- [ ] Pre-roll test: spend at least a minute in Stealth and say “before”; start
      Capture, say “after”, then stop it. Download/play the resulting WAV and
      confirm both markers are present in chronological order.
- [ ] Leave Stealth running beyond five minutes. Start and stop Capture, then
      confirm the WAV begins near the last five minutes—not when Stealth began.
- [ ] Confirm Stealth audio alone never enters the upload queue. A file is
      queued only after an explicit Capture is stopped.
- [ ] The display can dim in Stealth/Capture, but the device does not enter
      light sleep while the microphone is active.
- [ ] With Wi-Fi available, the outbox count falls only after a successful
      server response.
- [ ] Mission Control shows the capture and transcript. If no Groq key is set,
      enter a manual transcript and confirm it reaches `ready`.
- [ ] With Gemini configured, extracted items appear in Memory review and on
      the watch.
- [ ] Approve a task, event, reminder, and note. Confirm they land in Today,
      Calendar, or Saved notes as appropriate.
- [ ] Reject a proposal and confirm it disappears without creating an item.
- [ ] Reboot the watch with queued audio and confirm it uploads later.

## Battery and regression

- [ ] Timer, alarms, clock, quick settings, touch wake, and BOOT wake still work.
- [ ] Silent mode never opens the microphone.
- [ ] No capture file is deleted after an HTTP/authentication failure.
- [ ] Observe one normal day in Silent and Stealth mode and record battery loss.
