# AURA OS — Master Project Plan

**Project:** A from-scratch embedded OS for the Waveshare ESP32-S3-Touch-AMOLED-2.06,
turning it into a wrist-worn personal assistant ("Jarvis on your wrist").

**Architecture in one sentence:** the watch is a thin sensor + display + fast UI
client; a VPS-hosted companion server is the brain that does everything the
ESP32-S3 physically cannot (continuous transcription, LLM extraction, calendar
sync, long-term storage).

This document is the full build plan, Phase 1 through Phase 6, plus a forward
roadmap for extending the system well beyond the original spec. Each phase
lists: goal, deliverables, detailed task breakdown, hardware/software touched,
exit criteria (Definition of Done), risks, and rough effort sizing.

---

## 0. Vision & Guiding Principles

1. **Thin watch, smart server.** If a task needs continuous audio processing,
   LLM inference, or talking to a third-party API with OAuth, it belongs on
   the server, not the MCU.
2. **No small clicks.** Every tappable element is large, obvious, and forgiving.
   No nested menus more than one level deep.
3. **Battery first.** True-black AMOLED UI, aggressive sleep, no idle
   radio chatter. Every new feature is evaluated for its battery cost before
   it's evaluated for its usefulness.
4. **Human-in-the-loop AI.** The AI Memory system never silently writes to
   your calendar or reminders. It proposes; you approve. Trust is earned, not
   assumed.
5. **Own your data.** Custom database, self-hosted server, full export at any
   time. No dependency on a company staying in business.
6. **Ship phase by phase.** Every phase ends with a fully working watch —
   never a half-broken one. Stub tiles are honest placeholders, not missing
   features that crash.

---

## 1. System Architecture (recap, applies to every phase below)

```
┌─ WATCH (ESP32-S3R8) ───────────────┐        ┌─ AURA SERVER (VPS) ──────────────┐
│ Lock face + passcode + app launcher│  WiFi  │ FastAPI (or similar) + DB         │
│ Watch face + focused timer         │◄──────►│ STT worker (Groq Whisper /        │
│ Power mgr: dim/sleep/touch wake    │  REST  │            faster-whisper)        │
│ 5-min pre-roll + capture → TF card │   +    │ Extraction worker (Gemini Flash)  │
│ Gemini Live streaming (PTT)        │  WS    │ CalDAV (Apple) + MS Graph bridge  │
│ BLE ANCS client (iPhone notifs)    │        │ Mission Control DB + web editor   │
│ Compact synced-state cache         │        │ Transcript vault + export API     │
└─────────────────────────────────────┘        │ OTA firmware host                 │
              ▲ BLE                            └────────────────────────────────────┘
        ┌─────┴─────┐
        │  iPhone   │
        └───────────┘
```

**Hardware inventory (fixed across all phases):**
- ESP32-S3R8, dual-core LX7 @ 240 MHz, 8MB octal PSRAM, 32MB flash
- 410×502 AMOLED (CO5300 driver, QSPI) + FT3168 capacitive touch (I2C)
- QMI8658 6-axis IMU, PCF85063 RTC, AXP2101 PMU (battery + PWR button)
- ES8311 audio codec + ES7210 echo-cancelling mic front-end, dual mic array
- TF card slot, BOOT + PWR side buttons, USB-C

**Server inventory (introduced in Phase 3, used through Phase 6):**
- VPS: Oracle Cloud Free Tier (Ampere A1, up to 4 vCPU / 24GB RAM, $0/mo)
  or Hetzner CX22 (~€4/mo) as paid fallback
- STT: Groq Whisper API (~$0.04/hr audio) primary, self-hosted faster-whisper
  free fallback
- Extraction LLM: Gemini Flash (free tier)
- Live conversation: Gemini Live API (WebSocket, streamed from watch directly
  — server is not in this hot path)
- Database: custom schema, SQLite to start, promotable to Postgres if needed
- Target recurring cost: **$0–5/month**

---

## 2. Phase-by-Phase Plan

Each phase section follows the same template:
- **Goal** — the one-sentence outcome
- **What ships** — user-visible features at the end of the phase
- **Task breakdown** — the actual work items, grouped by subsystem
- **Definition of Done** — how you know the phase is complete
- **Risks / watch-outs** — things likely to bite
- **Rough effort** — solo-developer estimate, evenings/weekends pace

---

### Phase 1 — Foundation ✅ (delivered scaffold; hardening in progress)

**Goal:** A buildable, flashable, stable watch OS with the core UI shell,
clock functions, and power model — no networking, no AI yet.

**What ships:**
- Watch face (time, date, battery, mode indicator)
- Focused countdown timer
- Glanceable lock face, 4-digit passcode, and static 2x3 app launcher
- Power model: ACTIVE → DIM → SLEEP, light sleep, touch/button wake to lock face
- BOOT button back/home routing
- Placeholder tiles for Calendar / Mission Control / Memory (phase-gated,
  clearly labeled, non-crashing)

**Task breakdown:**
- *Build system:* root CMakeLists, sdkconfig defaults tuned for S3R8 octal
  PSRAM, dual-OTA + storage partition table
- *Core services (`aura_core`):* esp_event bus, NVS settings wrapper,
  PCF85063 ↔ system time bridge
- *Power (`power_mgr`):* inactivity FSM, light sleep entry/exit, software
  double-tap gate on TP_INT, BOOT button short/long press detection
- *UI (`ui`):* LVGL 9 static-screen shell, true-black theme, shared widget helpers
  (big buttons, list rows) enforcing the 80px minimum touch target
- *Apps:* watch face lock screen, timer, Mission Control, Capture Options,
  Calendar, Notifications, and Settings
- *Bring-up validation:* confirm BSP pin mapping against schematic, confirm
  QMI8658 I2C address via bus scan, confirm AXP2101 IRQ routing for PWR button

**Definition of Done:**
- [ ] Boots cleanly every time, no watchdog resets, no BSP API mismatches
- [ ] Full sleep/wake cycle survives 100+ repetitions without a crash
- [ ] Stopwatch, timer, and at least one alarm fire correctly across a
      real sleep/wake cycle (not just while awake)
- [ ] Double-tap wake feels right in the hand (not too twitchy, not sluggish)
- [ ] Battery drain over 8h idle measured and recorded as a baseline

**Risks / watch-outs:**
- BSP function name drift between library versions (low risk, fast fix)
- Any use of LVGL transform/scale/opacity-blend styles on frequently-redrawn
  widgets can starve the idle task and trip the watchdog — prefer real
  font assets over runtime transforms
- PWR button is on the PMU, not a GPIO — needs schematic-confirmed IRQ wiring

**Rough effort:** 1–2 weekends (mostly done; remainder is hardware validation
and the alarm time-picker UI).

---

### Phase 2 — Local Hardening & Polish

**Goal:** Everything that makes the *watch alone* (no server yet) feel like a
finished product, not a prototype.

**What ships:**
- PWR button wired through the AXP2101 IRQ: single press toggles
  Silent/Stealth or stops Capture, double press starts Capture, and long press
  remains the hold-to-talk stub (real function arrives Phase 5)
- Audio chime on alarms/timers via ES8311; optional haptic if a motor is
  ever added to the board
- Step counting from the QMI8658 accelerometer, shown on the watch face
- SNTP time sync over Wi-Fi on connect, written back to the PCF85063 so the
  watch keeps correct time even without a phone nearby
- Timezone and 12h/24h format settings
- Settings tile: brightness curve, sleep timeout, display test, factory reset
- Alarm time editor (roller picker) — no more "new alarm = right now"
- RTC-chip hardware alarm wake, so alarms can fire from a deeper sleep state
  than light sleep, improving battery life further
- Custom ~110px digits-only font for the watch face (real asset, not a
  runtime transform) for a properly "watch-like" look

**Task breakdown:**
- *Power:* confirm and wire AXP2101 IRQ GPIO; implement press-length
  detection sourced from PMU interrupt registers, not polling
- *Audio:* minimal I2S playback path for short WAV/PCM chime files stored in
  the SPIFFS storage partition
- *Sensors:* QMI8658 step-counting algorithm (simple peak-detection is
  enough for v1), exposed via the event bus as a periodic step-delta event
- *Time:* Wi-Fi provisioning flow (BLE-based or captive portal), SNTP client,
  timezone database subset (or manual UTC-offset setting to start simple)
- *UI:* settings tile with grouped rows, roller-based time picker component
  reused by both alarms and any future time-entry need
- *Fonts:* generate a condensed numeral font via LVGL's font conversion tool,
  bundle as a C array, swap into the watch face tile

**Definition of Done:**
- [ ] PWR button reliably distinguishes single, double, and long presses with
      no missed or duplicate events across 50 test presses
- [ ] Time stays accurate across a multi-day unplugged test (RTC-backed)
- [ ] Step count roughly matches phone step count over a test walk (±15%)
- [ ] Alarms fire correctly even when the watch has been asleep for hours
- [ ] Watch face reads clearly at arm's length in direct sunlight and in
      a dark room

**Risks / watch-outs:**
- I2S + light sleep interaction: audio playback must fully complete (or be
  cancelled cleanly) before re-entering sleep, or you risk a stuck DMA state
- Wi-Fi provisioning UX on a screen this small needs real care — a QR-code
  based provisioning flow (scan with phone camera) is worth considering over
  on-watch text entry
- Step counting drains a small amount of battery continuously; gate it
  behind a setting if it turns out to matter

**Rough effort:** 2–3 weekends.

---

### Phase 3 — Connected Watch

**Goal:** Stand up the companion server and turn the watch into a genuinely
networked device: calendar sync, Mission Control, and iPhone notifications.

**What ships:**
- Aura Server v1 running on the chosen VPS: REST API + auth token, health
  endpoint, `/digest` endpoint the watch polls
- Calendar tile: merged view of Apple (CalDAV) + Microsoft (Graph API)
  calendars, next-48-hours on the watch face, full month view on the tile
- Mission Control tile: Today digest (top to-dos across Projects / School /
  Personal), drill-down lists, quick check-off actions
- Mission Control web editor: full CRUD for projects, assignments, classes,
  goals — editing happens on the web, the watch is read-mostly + quick actions
- iPhone notifications via BLE ANCS: app name, title, body, dismiss action,
  shown in the swipe-up panel
- OTA groundwork: server can host a firmware binary; watch can check version
  and download (activation still manual in this phase, full auto in Phase 6)

**Task breakdown:**
- *Server scaffold:* FastAPI (or equivalent) app, SQLite schema for
  users/events/todos/projects/classes/goals/notifications, auth token issuing
- *Calendar bridge:* CalDAV client against iCloud (app-specific password),
  Microsoft Graph client (device-code OAuth flow), a merge/normalize layer
  that produces one unified event feed
- *Mission Control backend:* CRUD API, simple ranking logic for "Today"
  digest (due-soonest first, then priority flag)
- *Mission Control web UI:* minimal responsive web app (could be server-
  rendered HTML + htmx to avoid a heavy frontend stack) for editing
- *Watch networking:* Wi-Fi connection manager, HTTPS client with the auth
  token, `/digest` polling on a sane interval (e.g., every 10 min while awake,
  on-wake if stale), local cache so the watch face still works offline
- *BLE ANCS:* ESP32-S3 as a BLE peripheral, iOS pairing flow, ANCS
  characteristic subscription, notification parsing and display, dismiss
  action write-back
- *Calendar/Mission UI:* replace the Phase 1 stub tiles with real list views,
  reusing the `aura_ui_list_row` pattern established in Phase 1

**Definition of Done:**
- [ ] Watch shows real calendar events from both Apple and Microsoft within
      one minute of them being created on the source calendar
- [ ] A to-do checked off on the watch reflects on the web UI within seconds,
      and vice versa
- [ ] iPhone notifications appear on the watch within a couple of seconds of
      arriving on the phone, and dismissing on the watch dismisses on the phone
- [ ] Server survives a reboot without losing data (persistence confirmed)
- [ ] Watch face still shows time/date correctly with Wi-Fi off or server
      unreachable (graceful degradation, not a crash or blank screen)

**Risks / watch-outs:**
- ANCS is the most fragile part of this phase — iOS BLE pairing/bonding has
  a well-known list of gotchas (bonding cache invalidation, re-pairing after
  iOS updates). Budget real debugging time here.
- CalDAV app-specific passwords need to be stored securely on the server;
  Microsoft Graph tokens need refresh-token handling — don't hardcode
  long-lived credentials
- Decide early whether calendar writes (creating events from the watch) are
  in scope for this phase or deferred — recommend deferring; read-only sync
  first, writes come naturally once Phase 4's approval queue exists
- Watch-side polling cadence is a battery/freshness tradeoff — tune it,
  don't guess

**Rough effort:** 3–5 weekends (ANCS and the OAuth dances are the long poles).

---

### Phase 4 — AI Memory

**Goal:** Turn ambient conversation into structured calendar entries and
reminders, with a human always in the approval loop.

**What ships:**
- Capture Mode: mic array → voice-activity detection → compressed audio
  chunks written to the TF card in a ring buffer
- Batch uploader: opportunistic upload to the server (charging, or on a
  strong Wi-Fi connection), so the watch is never streaming live audio
  continuously
- Server STT pipeline: Groq Whisper (primary) or self-hosted faster-whisper
  (fallback), producing timestamped transcripts
- Server extraction worker: Gemini Flash reads each transcript and proposes
  structured items — calendar events, reminders, to-dos — into a **pending
  approval queue** (nothing writes to your real calendar unapproved)
- "What did I miss" review screen on the watch: swipe through proposed items,
  approve or reject with a single tap each
- Transcript vault on the server, browsable/searchable from the web UI
- Visible on-watch recording indicator whenever mics are live (trust +
  likely legal requirement depending on your jurisdiction and who's present)

**Task breakdown:**
- *Audio capture:* I2S driver for the ES8311/ES7210 front end, a five-minute
  overwriting Stealth ring, and explicit Capture that preserves the pre-roll
  and continues until the user stops it. Add Opus or ADPCM later to reduce size.
- *Ring buffer + upload:* file rotation policy on the TF card, upload queue
  with retry/backoff, delete-after-confirmed-upload to keep storage bounded
- *Server STT worker:* job queue (even a simple one) that pulls uploaded
  audio, calls Groq Whisper (or local faster-whisper), stores transcript
  with timestamps
- *Server extraction worker:* prompt design for Gemini Flash to output
  structured JSON (event title, datetime, confidence, source transcript
  excerpt), validation of the LLM's output before it ever reaches the queue
- *Approval queue:* API + watch UI for reviewing pending items; approved
  events flow into the same calendar write path Phase 3 deferred
- *Privacy/indicator:* a persistent, unmissable on-screen indicator (not just
  a small icon) whenever Capture Mode is active with mics live

**Definition of Done:**
- [ ] A spoken reminder ("remind me to call the dentist tomorrow at 3") ends
      up as a correctly-dated pending item within a reasonable delay
      (minutes, not hours)
- [ ] False-positive rate on extraction is low enough that approving items
      feels like a quick tap, not a chore of rejecting garbage
- [ ] TF card storage never fills up uncontrolled — old, uploaded audio is
      cleaned up automatically
- [ ] Recording indicator is genuinely impossible to miss when Capture Mode
      is on
- [ ] Silent Mode is a true hardware/software mic-off — verified by checking
      the I2S clock is actually stopped, not just muted in software

**Risks / watch-outs:**
- This is the phase with the most non-technical weight: recording other
  people has consent implications that vary by location — worth deciding
  your own policy (e.g., audible chime when Capture Mode starts) before
  relying on this daily
- Continuous Stealth costs battery and writes the SD card constantly; measure
  real runtime and SD reliability before treating it as an all-day mode
- LLM extraction will occasionally hallucinate a date/time — the approval
  queue exists specifically so this never matters, don't skip it under
  time pressure
- Battery cost of Stealth is real (mic front-end + continuous SD writes)—measure
  it and set expectations before relying on it all day

**Rough effort:** 4–6 weekends (the extraction prompt and approval UX will
take iteration).

---

### Phase 5 — Conversation Mode

**Goal:** The actual "talk to your watch" Jarvis moment, via Google's Gemini
Live API.

**What ships:**
- PWR long-press → Wi-Fi wakes (if not already up) → WebSocket session to
  Gemini Live opens
- Live two-way audio: mic PCM stream up, TTS audio stream down, played
  through the ES8311, with ES7210 echo cancellation so the watch can listen
  and talk at once without feedback
- Function-calling bridge: the model can call back into the Aura Server to
  create a reminder, look up "what's on my calendar today," check a Mission
  Control to-do, etc. — same backend the watch UI already uses
  from Phase 3/4
- Visual state on-watch during a session: listening / thinking / speaking
  indicator, and a clean way to end the session (tap to hang up)

**Task breakdown:**
- *Audio streaming:* 16kHz PCM capture path distinct from Phase 4's rolling
  storage path (this one is streamed for the session's duration),
  WebSocket framing to the Gemini Live API, downstream audio playback
  buffering to avoid choppy TTS
- *Session management:* connect/reconnect handling, timeout after silence,
  clean teardown on PWR press or Wi-Fi loss
- *Function calling:* define the tool schema Gemini Live can call (create
  reminder, query calendar, query Mission Control, set a timer/alarm — even
  local ones), route those calls to the Aura Server's existing API from
  Phase 3/4, return results back into the conversation
- *UI:* a dedicated conversation overlay — waveform or simple state icon,
  large "end call" tap target, nothing else competing for attention

**Definition of Done:**
- [ ] Round-trip latency (you finish speaking → the watch starts responding)
      feels conversational, not laggy
- [ ] Echo cancellation actually works — no feedback loop, no the-watch-
      hears-itself artifacts
- [ ] At least three function calls work end-to-end (e.g., "what's my next
      meeting," "add a reminder," "start a 10-minute timer")
- [ ] A dropped Wi-Fi connection mid-session fails gracefully (clear message,
      no hang, no crash) rather than silently hanging
- [ ] Session cleanly ends and returns to normal watch behavior every time

**Risks / watch-outs:**
- This is the highest continuous-power-draw mode on the device by far — set
  expectations that sessions are short, purposeful interactions, not
  standing background calls
- Gemini Live API details (auth, exact WebSocket protocol, function-calling
  schema) can change — validate against current Google documentation at
  build time rather than assuming past API shapes hold
- Audio buffering/jitter handling is genuinely one of the harder embedded
  audio problems — budget real time for tuning buffer sizes

**Rough effort:** 4–6 weekends (audio pipeline tuning is the long pole here
too).

---

### Phase 6 — Ops, Scale & Polish

**Goal:** The things that make this a *system you can live with long-term*,
not just a cool demo.

**What ships:**
- OTA updates: watch checks the server for new firmware, downloads to the
  inactive OTA partition, verifies, and switches — no more USB cable for
  updates
- Data export: one-tap (or one-API-call) export of everything — transcripts,
  calendar history, Mission Control data, settings — as JSON/CSV
- Retention settings: how long raw audio/transcripts are kept before
  automatic deletion, configurable per your comfort level
- Speaker diarization (optional): pyannote self-hosted on the server, adding
  "who said what" to Memory transcripts — batch job, doesn't need to be fast
- Battery telemetry dashboard: track battery health and drain patterns over
  time on the web UI, useful for catching a regression early
- Server hardening: backups, basic monitoring/alerting so you know if the
  brain goes down, HTTPS everywhere, rate limiting on the API

**Task breakdown:**
- *OTA:* server-side firmware hosting + versioning, watch-side download-
  verify-switch logic using the dual OTA partitions already reserved
  in Phase 1's partition table
- *Export API:* comprehensive dump endpoints, sensible file formats,
  clear documentation of what's included
- *Retention:* scheduled cleanup jobs on the server respecting user-set
  windows, watch-side TF card cleanup already established in Phase 4
- *Diarization:* pyannote pipeline as a batch worker alongside the existing
  STT worker, storing speaker labels alongside transcript segments
- *Telemetry:* lightweight metrics collection (battery %, charge cycles,
  wake counts) posted alongside the existing `/digest` traffic, simple
  charts on the web UI
- *Server ops:* automated backups (even a nightly cron + off-box copy is
  enough at this scale), uptime monitoring (a free tier of something like
  UptimeRobot), basic log rotation

**Definition of Done:**
- [ ] A firmware update can be pushed and installed without touching a
      USB cable, and a bad update can be rolled back
- [ ] Full data export produces a complete, readable archive of your data
- [ ] Retention settings actually delete data on schedule (verified, not
      assumed)
- [ ] Server survives a VPS reboot and a power blip without manual
      intervention
- [ ] You'd trust this system to run unattended for a month

**Risks / watch-outs:**
- OTA is one of the few places a bug can genuinely brick the device —
  test the rollback path as thoroughly as the update path itself
- Diarization is CPU-heavy; keep it strictly as a background batch job, not
  something blocking the approval-queue flow from Phase 4
- "Backups" only count if you've tested a restore, not just a backup file
  existing

**Rough effort:** 3–4 weekends, though OTA reliability testing can quietly
eat more time than expected.

---

## 3. Extending Beyond Phase 6

Once the core system is solid, here's where this project can genuinely grow.
These aren't required, but they're the natural next layer — organized by
theme so you can pick what excites you.

### 3.1 Health & Body Awareness
- **Heart rate / SpO2**: would require an add-on sensor (the current board
  has no PPG sensor) — a small daughterboard or a second I2C peripheral
- **Sleep tracking**: derivable from the IMU (motion) + mic (silence/snoring
  detection) without new hardware, feeding into a nightly summary
- **Posture/activity nudges**: "you've been sitting for 90 minutes" using
  just the existing IMU
- **Guided breathing / focus timer**: a genuinely simple, high-value addition
  using haptics (if added) or gentle audio cues

### 3.2 Ecosystem & Multi-Device
- **Web dashboard as a first-class client**: everything the watch can do,
  also accessible from a browser — useful for reviewing Memory approvals on
  a bigger screen
- **Multiple watches, one brain**: the server-brain architecture already
  supports this — a second watch (or a phone-only client) could share the
  same Mission Control and Memory data
- **Family/shared mode**: shared calendars and to-do lists between multiple
  people's Aura Servers, with permission boundaries

### 3.3 Voice & Interaction
- **Custom wake word** (on-device, via ESP-SR): "Hey Aura" to trigger
  Conversation Mode without a button press — genuinely feasible on this
  chip for a small fixed vocabulary, unlike full transcription
- **Gesture shortcuts**: wrist-flick (via IMU) to silence an alarm or dismiss
  a notification without touching the screen
- **Personality/tone customization**: system-prompt-level customization of
  how the assistant responds in Conversation Mode

### 3.4 Intelligence & Automation
- **Proactive briefings**: a "good morning" digest spoken or shown when you
  first interact with the watch each day — today's weather, first meeting,
  top to-do
- **Cross-referencing Memory with Mission Control**: "you mentioned this
  project three times this week and haven't touched its to-do list" style
  gentle nudges
- **Location-aware reminders**: "remind me when I get home" using phone
  location via a companion app or BLE proximity, resolved server-side
- **Habit tracking**: lightweight streaks/goals system layered on top of the
  existing Personal/Goals data model

### 3.5 Platform & Developer Experience
- **A local "skills" system**: a lightweight plugin format for the server
  (new function-calling tools for Conversation Mode) so you can add
  integrations (smart home, banking summaries, whatever) without touching
  the watch firmware at all
- **Public API + webhook support**: let other services push data into your
  Aura Server (e.g., a fitness tracker pushing workout data into Mission
  Control)
- **Config-as-code**: your Mission Control structure, settings, and
  automations expressed as a versioned config file you can back up and
  restore independently of the database

### 3.6 Hardware Evolution (if you ever revisit hardware choices)
- A future custom PCB revision could add: a haptic motor (currently absent
  on this dev board — meaningfully improves alarms/notifications), a PPG
  health sensor, a larger battery, and possibly an always-on low-power
  display layer for the watch face so the AMOLED doesn't need to fully wake
  just to check the time
- Voice wake-word + always-listening-for-wake-word-only is far more power-
  efficient in a dedicated low-power core (like an ESP32-C3 co-processor)
  than doing it on the main S3 — worth considering if Conversation Mode
  becomes a daily habit

---

## 4. Cross-Cutting Concerns (apply across all phases)

**Battery budgeting:** every new feature should come with a rough "what does
this cost per hour of use" estimate before it's built, not after.

**Privacy posture:** decide once, apply everywhere — what's stored, for how
long, who can see it, and how it's deleted. Phase 4 introduces the first real
privacy-sensitive data (audio); Phase 6's retention settings are the release
valve.

**Testing discipline:** each phase's Definition of Done is a checklist, not a
vibe — actually run through it before calling a phase "done," especially the
sleep/wake and data-integrity items, since those are the ones that fail
silently.

**Documentation habit:** keep `ARCHITECTURE.md` and `ROADMAP.md` (already
started in Phase 1) updated as you go — future-you debugging a weird issue
six months from now will thank present-you.

---

## 5. Suggested Milestone Order (if you want one linear path)

1. Finish Phase 1 hardening (real hardware validation, alarm picker)
2. Phase 2 in full (this is what makes daily wear pleasant)
3. Stand up the server skeleton early in Phase 3 — even before calendar
   sync is done, having `/digest` and auth working de-risks everything after
4. Calendar sync before Mission Control (higher personal value, and Mission
   Control's UI patterns benefit from lessons learned on the Calendar tile)
5. ANCS notifications last within Phase 3 — it's valuable but the most
   fiddly, don't let it block calendar/mission progress
6. Phase 4's approval-queue UX is worth prototyping with fake data *before*
   the STT/extraction pipeline is fully wired — validate the interaction
   feels right first
7. Phase 5 only once Phase 3's server API is stable — Conversation Mode
   leans on it heavily for function calling
8. Phase 6 can start in parallel with Phase 4/5 for the low-risk pieces
   (export API, retention settings) — OTA and diarization are fine to leave
   until last
