# R1 static UI validation

Flash this branch only after confirming the build succeeds.

## Expected watch behavior

1. Boot lands on a true-black lock face with Wi-Fi/Phone status dots, battery,
   large spaced time, date below it, steps/event, notification count, unlock
   hint, and the audio mode at bottom.
2. Notification title/body content never appears on the lock face. Tap the
   notification-count button to open detailed notification cards; lock-screen
   swipe-down does nothing.
3. The notification screen has a title/Clear All top bar and Home bottom bar.
   Clear All removes local cards even if the phone disconnected. Home returns
   to the lock face when notifications were opened while locked.
4. Dismiss one notification; the panel, lock count, and launcher count update.
5. Tap the lock face to open the large white-on-black numeric PIN keypad.
6. Unlock to the static launcher containing Mission, Timer, Capture, Calendar,
   Talk, and Settings. There is no Aura name/mark, Notifications, or Lock button.
   All six apps use distinct code-drawn icons and retain their individual color.
7. The unlocked launcher top row shows Wi-Fi status, Phone/BLE status, and alert
   count. Swipe down from this top row to open notifications; Home returns to
   the unlocked launcher rather than bypassing or re-triggering the PIN.
8. Open each app. The mode badge remains visible at bottom-center with a hollow
   Silent, amber Stealth, or red Capture indicator. An inward
   swipe from either edge returns to the launcher.
9. Talk shows the five-bar voice meter foundation and clearly reports that
   Gemini Live is staged for R4; it does not start an incomplete session.
10. In Settings, switch Notification sound Off and On. A newly arriving iPhone
   notification plays one short cue only when enabled; updates to the same
   notification do not replay it.
11. While already on the lock face, press BOOT once. In Silent the panel enters
    light sleep; in Stealth/Capture the panel blanks while recording continues.
    Press BOOT once again and confirm the face wakes without immediately going
    dark again. BOOT retains its normal Back behavior everywhere else.
12. Let the display dim, wake by touch, lock/unlock repeatedly, and confirm no
    black/pink lines, overlapping screens, or animated full-screen transitions.
13. Settings is grouped into Display, Connections, Alerts, and Security with
    compact controls. There is no timezone/UTC slider.
14. Confirm no square placeholder glyphs appear in Timer, Capture, Mission,
    notification, status, or lock-face text.

## Serial log failures to report

- `spi transmit`, `DMA TX underflow`, allocation failures, watchdog resets
- speaker open/write failures
- ANCS disconnect loops or notification event drops

Do not continue to R2 until the UI survives at least 25 lock/shade/app/wake
cycles without display corruption.
