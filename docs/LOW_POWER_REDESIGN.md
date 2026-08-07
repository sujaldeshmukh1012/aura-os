# Aura OS low-power redesign

This document supersedes the earlier swipe-heavy UI plan. The stable pre-redesign
baseline is Git commit `a80694e` on private GitHub `main`.

## Product rules

- The AMOLED lock face is the default destination and uses true black pixels.
- Screens are static objects switched without full-screen transition animation.
- Only changed labels/cards are invalidated; the clock redraws once per minute.
- `silent`, `stealth`, `capture`, or `live` remains visible at bottom-center.
- Lock-face notifications show a count only. A downward lock-face swipe opens
  the detailed notification shade; notifications are not a launcher app.
- The PIN keypad uses large, high-contrast controls inside the curved safe area.
- Notification feedback is a short speaker cue, never a vibration, and can be
  disabled in Settings.
- BLE is the idle transport. Wi-Fi is leased only for an explicit upload/live
  session or a 30-minute background synchronization window, then shut down.

## Runtime state model

```text
LOCK FACE --tap--> PIN --> LAUNCHER --> APP
    |
    +--swipe down--> NOTIFICATION SHADE --back/up--> LOCK FACE

SILENT:  microphone and Wi-Fi off; BLE low duty cycle
STEALTH: microphone on; five-minute IMA-ADPCM ring in PSRAM; no SD writes
CAPTURE: freeze pre-roll; append capture; finalize durable file on Stop
LIVE:    Wi-Fi on; BLE paused; temporary Gemini stream; return to SILENT
```

## Implementation phases

### R1 — Static UI and notification audio

- Rebuild lock face, notification counter/shade, launcher, and PIN screen.
- Remove Notifications from the launcher and add the Talk application entry.
- Add the persistent mode badge and notification-sound setting.
- Validate redraw stability, navigation, notification add/remove, and 100 wake
  cycles before continuing.

### R2 — Connectivity leases

- Replace boot-time permanent Wi-Fi with an explicit lease manager.
- BLE remains connected/advertising while idle.
- Acquire Wi-Fi for queued capture upload, manual refresh, Live, and one short
  sync every 30 minutes. Release Wi-Fi after completion or timeout.
- Cache server data so every watch screen remains useful offline.

### R3 — PSRAM stealth recorder

- Encode microphone PCM to 4-bit IMA ADPCM as it arrives.
- Keep 300 seconds in an approximately 2.4 MB PSRAM circular buffer.
- Do not mount or write microSD during ordinary Stealth operation.
- On Capture, freeze the ring boundary, write the ordered pre-roll and appended
  audio to a temporary file, finalize atomically, and queue it for upload.
- If PSRAM allocation fails, fail closed to Silent rather than wearing the SD.

### R4 — Gemini Live conversation

- Aura Server provisions a restricted, short-lived Gemini Live token; the
  long-lived Gemini API key never enters firmware.
- The Talk app explicitly starts/stops a session; BOOT also stops it.
- Entering Live gracefully stops Stealth/Capture and selects Silent semantics.
- Watch sends 16 kHz mono PCM directly to Gemini and plays returned 24 kHz PCM.
- Input/output transcripts are buffered for the session. After Stop and final
  model output, upload transcript plus metadata to Aura; discard live PCM.
- Do not request session resumption, minimizing provider-side retained state.

### R5 — Phone companion contract

- Define a small versioned BLE protocol for state deltas, commands, notification
  summaries, calendar cache, and Wi-Fi lease hints.
- The future phone app owns account integrations and continuously talks to Aura.
- The watch never receives Google, Apple, or Microsoft account credentials.

## Release gates

Every phase must build, pass host/server tests, flash independently, and have a
written watch validation checklist. Battery current, heap/PSRAM headroom, audio
underruns, display corruption, reconnect behavior, and data durability are
release blockers rather than deferred polish.
