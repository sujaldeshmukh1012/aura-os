# R1 static UI validation

Flash this branch only after confirming the build succeeds.

## Expected watch behavior

1. Boot lands on a true-black lock face with Wi-Fi/Phone status dots, battery,
   large spaced time, date below it, steps/event, notification count, unlock
   hint, and the audio mode at bottom.
2. Notification title/body content never appears on the lock face.
3. Swipe downward starting in the top strip to open detailed notification
   cards. A tap/release inside that strip must not open the PIN screen.
4. Dismiss a notification; the shade and lock-face count update immediately.
5. Tap the lock face to open the large white-on-black numeric PIN keypad.
6. Unlock to the Aura-marked static launcher containing Mission, Timer, Capture,
   Calendar, Talk, and Settings. There is no Notifications or Lock button;
   Capture uses REC and Talk uses AI so they are visually distinct.
7. Open each app. The mode badge remains visible at bottom-center. An inward
   swipe from either edge returns to the launcher.
8. Talk shows the five-bar voice meter foundation and clearly reports that
   Gemini Live is staged for R4; it does not start an incomplete session.
9. In Settings, switch Notification sound Off and On. A newly arriving iPhone
   notification plays one short cue only when enabled; updates to the same
   notification do not replay it.
10. Let the display dim, wake by touch, lock/unlock repeatedly, and confirm no
    black/pink lines, overlapping screens, or animated full-screen transitions.
11. Settings is grouped into Display, Connections, Alerts, and Security with
    compact controls. There is no timezone/UTC slider.
12. Confirm no square placeholder glyphs appear in Timer, Capture, Mission,
    notification, status, or lock-face text.

## Serial log failures to report

- `spi transmit`, `DMA TX underflow`, allocation failures, watchdog resets
- speaker open/write failures
- ANCS disconnect loops or notification event drops

Do not continue to R2 until the UI survives at least 25 lock/shade/app/wake
cycles without display corruption.
