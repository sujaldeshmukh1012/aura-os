# Phase 2 hardware validation

Flash firmware `0.2.0`, open the serial monitor, and run this short acceptance
pass before calling the phase physically complete.

## Watchface and hardware

- Battery is no longer fixed at 50%; percentage changes and shows charging.
- PWR behavior is superseded by Phase 4: single press toggles Silent/Stealth
  (and stops Capture); double press starts Capture from Silent or Stealth.
- The watchface shows today's step count; walk 30–50 normal steps and confirm
  it increases approximately with movement and does not climb while resting.
- Step count and preferences survive a reboot; steps reset on the next local day.

## Clock and audio

- Swipe right to Clock, add an alarm with the hour/minute rollers, save it,
  reopen it, disable/enable it, then delete it.
- Set a timer for one minute. At completion the full-screen ring UI appears,
  the two-tone speaker chime plays, and Dismiss releases the wake lock.
- Set an alarm one minute ahead and repeat the same ring test.

## Settings and network

- Swipe left through the tiles to Settings.
- Change brightness, clock format, UTC offset, and screen-off duration; reboot
  and confirm all four persist.
- Tap the Wi-Fi row, join the `Aura-Setup-XXXX` hotspot on a phone, browse to
  `http://192.168.4.1`, and submit the 2.4 GHz Wi-Fi credentials.
- Confirm the row changes to `Wi-Fi: connected`, network time appears on the
  face, and remains correct after reboot without Wi-Fi.

## Power regression

- Verify horizontal tile swipes and vertical quick-settings swipes still work.
- Verify dim, screen-off, BOOT wake, and double-tap wake at each selectable
  timeout. Confirm Flashlight restores the saved brightness when closed.
