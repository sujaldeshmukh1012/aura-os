# AURA OS — Architecture

Personal-assistant watch firmware for the Waveshare ESP32-S3-Touch-AMOLED-2.06,
paired with a companion server ("Aura Server") that does everything an MCU can't.

## System split

The watch is a **sensor + display + fast UI**. The server is the **brain**.

    ┌─ WATCH (ESP32-S3) ─────────────┐      ┌─ AURA SERVER (VPS) ─────────────┐
    │ LVGL UI · clock, timer, apps   │ WiFi │ FastAPI + SQLite/Postgres       │
    │ 5-min pre-roll + audio capture │◄────►│ STT: Groq Whisper (or           │
    │ Gemini Live streaming (PTT)    │  WS/ │      self-hosted faster-whisper)│
    │ BLE ANCS (iPhone notifications)│ REST │ Extraction: Gemini Flash        │
    │ Compact synced state cache     │      │ CalDAV (Apple) + MS Graph       │
    └────────────────────────────────┘      │ Mission Control DB + web UI     │
                    ▲ BLE                   │ Transcript vault + exports      │
              ┌─────┴─────┐                 │ OTA firmware host               │
              │  iPhone   │                 └─────────────────────────────────┘
              └───────────┘

### Why the server exists
Continuous transcription, diarization, LLM extraction, and calendar-provider
APIs are impossible or impractical on an ESP32-S3. The watch stays thin,
responsive, and battery-sane; the server holds truth and history.

## Cost plan
- VPS: Oracle Cloud Free Tier (Ampere A1) preferred -> $0. Fallback Hetzner CX22 (~€4/mo).
- STT: Groq Whisper (~$0.04/hr of audio) or self-hosted faster-whisper ($0).
- Extraction LLM: Gemini Flash free tier.
- Target total: **$0–3/month**.

## Watch firmware layout

    main/                boot sequence, battery service, alarm ring modal
    components/
      aura_core/         event bus (esp_event), NVS settings, PCF85063 <-> systime
      power_mgr/         ACTIVE -> DIM -> SLEEP; light sleep;
                         touch/button wake on TP_INT; BOOT button routing
      connectivity/      HTTPS digest cache and durable audio outbox uploader
      hardware/          PMU, sensors, audio codec and microSD audio ring
      notifications/     bonded NimBLE ANCS client and notification cache
      ui/                LVGL lock face, numeric passcode, launcher, apps,
                         capture review, settings + notification screens

### Navigation
    [watch face + compact notifications]  <- locked / wake destination
                    tap
             [4-digit PIN]
                    |
       [static 2x3 icon launcher]
        Mission  Timer  Capture
       Calendar  Notifications  Settings

    BOOT short returns from an app to the launcher. BOOT long locks immediately.
    An inward swipe beginning within 30 px of either side is also Back.
    Screens change visibility without framebuffer swipe animations.
    PWR single: Silent -> Stealth -> Silent; Capture -> save + Stealth.
    PWR double: Silent/Stealth -> Capture. PWR long: conversation (Phase 5).

### Power model
- AMOLED true-black theme; unlit pixels cost ~nothing.
- The locked watch face dims to an AMOLED-safe always-on view: true-black,
  minute-only refresh, and no animation. Interactive screens still time out to
  panel sleep + paused LVGL + light sleep.
- Wake sources: TP_INT (one deliberate touch) and BOOT button. Every wake
  returns to the locked watch face.
- Stealth and Capture own a wake lock: the screen may dim, but light sleep is
  disabled while the microphone and SD writer are active.
- Stealth continuously overwrites a five-minute PCM ring on the microSD card.
  Capture snapshots that pre-roll, appends new audio until stopped, then creates
  one WAV in the durable outbox. Neither mode live-streams audio.

### Audio modes
| Mode         | Mics / storage | Radio | Battery cost |
|--------------|----------------|-------|--------------|
| Silent       | hardware path closed | BLE; normal sync | minimal |
| Stealth      | continuous; rolling last five minutes on SD | BLE; queued uploads allowed | moderate |
| Capture      | preserves pre-roll and records until stopped | uploads deferred | moderate |
| Conversation | live, no pre-roll semantics | WiFi WS to Gemini Live | high, but momentary |

Privacy: every mic-live state is always visible on the watch face; Silent is a
hard mic-off. Recording other people may require consent depending on
jurisdiction—the visible indicator is there for a reason.

## Server (Phase 3+)
- `POST /api/v1/audio` idempotent raw uploads -> STT worker -> transcript store
- Extraction worker: transcript -> {events, reminders, todos} -> **pending
  approval queue** (nothing auto-writes to calendars; you approve on watch/web)
- `GET /digest` compact JSON the watch caches: next 48 h events, top todos,
  pending approvals, unread counts
- CalDAV (iCloud app-specific password) + Microsoft Graph (device-code flow)
- Web UI for Mission Control editing and data export (item 7)

## Hardware notes / VERIFY list
- Pins from Waveshare pin_config.h: LCD QSPI 4/5/6/7, SCLK 11, CS 12, RST 8;
  touch I2C SDA 15 / SCL 14, TP_INT 38, TP_RST 9; SD 1/2/3 (+CS 17 legacy SPI).
- PWR side button routes to AXP2101 PWRON -> read via PMU IRQ, not GPIO.
  VERIFY the AXP2101 IRQ pin on the schematic PDF.
- QMI8658 I2C address (0x6A vs 0x6B) — VERIFY on bus scan.
- BSP function names for PMU fuel gauge — adjust `battery_task` to your BSP
  version's API.
