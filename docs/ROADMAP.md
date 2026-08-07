# AURA OS — Roadmap

## Phase 1 — Reliable foundation (complete)
- [x] Project structure, partitions (dual OTA + 16MB storage), sdkconfig
- [x] BSP display + LVGL bring-up, black AMOLED lock face, numeric passcode,
      and static icon launcher
- [x] Watch face (time/date/battery/mode), minute-tick refresh
- [x] Focused countdown timer + ring modal (alarm and stopwatch UI retired)
- [x] Power manager: dim -> sleep FSM, light sleep, reliable touch wake,
      BOOT button back/home
- [x] Flash on hardware and verify display/touch/navigation
- [x] Alarm time editor (roller picker)
- [x] Prevent periodic time events from waking light sleep
- [x] Fix recurring-alarm deduplication across days
- [x] Validate RTC fields before use and reject invalid time writes
- [x] Synchronize persisted Capture/Silent mode across quick settings and face
- [x] Track LVGL-wide input inactivity and use independent wake locks
- [x] Embed firmware version and add a physical acceptance checklist

## Phase 2 — Polish + local hardening
- [x] AXP2101 battery gauge and polled PWR-key events (single/double/long press)
- [x] ES8311 chime for alarms/timers
- [x] QMI8658 low-power software pedometer with daily persistent count
- [x] Editable/deletable daily alarms with 12/24-hour formatting
- [x] SNTP over Wi-Fi writes back to PCF85063; persistent UTC offset
- [x] Settings tile: brightness, sleep timeout, clock format, captive Wi-Fi setup
- [x] Alarm wake remains reliable in Aura's light-sleep model. PCF85063 INT is
      not exposed as an ESP32 wake GPIO on this board, so RTC-triggered deep
      sleep is intentionally not enabled.

## Phase 3 — Connected watch
- [x] Aura Server v1 locally: FastAPI + SQLite, digest, health, bearer auth
- [x] Mission Control API/web editor and watch check-off actions
- [x] HTTPS watch client with persistent offline digest cache
- [x] Replace Calendar and Mission placeholder tiles with digest-backed UI
- [x] One-time Oracle Docker/Caddy deployment package
- [x] Oracle Docker/Caddy service running at `aura.sujalbhakare.com`
- [x] Durable data volume, container health check, restart policy, backup helper
- [x] Calendar bridge: iCloud CalDAV + MS Graph -> merged Calendar tile feed
- [x] BLE ANCS: compact lock-face preview + Notifications app with dismiss
- [ ] Physical validation: provider authorization, ANCS pairing/reconnect, and
      Oracle reboot persistence

## Phase 4 — AI Memory
- [x] Three-mode recorder: hard-off Silent, rolling five-minute Stealth, and
      explicit 16 kHz mono Capture with pre-roll on microSD
- [x] Durable outbox uploader with authenticated, idempotent HTTPS delivery
- [x] Groq Whisper STT -> transcript vault -> Gemini extraction -> approval queue
- [x] Approved tasks/reminders, events, and notes persist in Aura Server
- [x] Memory review screen on watch with approve/reject
- [x] Mission Control capture upload, manual transcript fallback, review, and
      saved-note UI
- [ ] Physical validation using `docs/PHASE3_4_VALIDATION.md`

## Phase 5 — Conversation Mode
- PWR long-press -> WiFi up -> Gemini Live API WebSocket
- 16 kHz PCM mic upstream (AEC via ES7210), TTS audio downstream via ES8311
- Function-calling into Aura Server (create reminder, query calendar, etc.) —
  this is the actual Jarvis moment

## Phase 6 — Ops
- OTA updates pulled from server (dual slots already partitioned)
- Data export endpoints (JSON/CSV dump of everything) + retention settings
- Diarization (pyannote) if wanted; battery telemetry dashboard
