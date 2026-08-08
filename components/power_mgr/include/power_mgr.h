#pragma once
#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Power manager: ACTIVE -> DIM (20 s) -> SLEEP (30 s), single-touch wake.
 *
 * Sleep strategy:
 *  - AMOLED into sleep mode + LVGL timers paused (biggest win: OLED off = 0 panel draw)
 *  - esp_light_sleep with GPIO wake on TP_INT (touch controller kept in
 *    low-power monitor mode) and on the BOOT button
 *  - TP_INT wakes immediately on one deliberate touch. The controller emits
 *    a short interrupt pulse, which makes post-wake double-tap confirmation
 *    unreliable on this board.
 *  - BLE (Phase 3, ANCS) stays alive across light sleep via automatic
 *    light-sleep + Wi-Fi/BT modem sleep; nothing needed here yet
 */
esp_err_t power_mgr_init(void);

typedef enum {
    POWER_WAKE_LOCK_USER       = 1U << 0,
    POWER_WAKE_LOCK_ALARM      = 1U << 1,
    POWER_WAKE_LOCK_FLASHLIGHT = 1U << 2,
    POWER_WAKE_LOCK_ASSISTANT  = 1U << 3,
    POWER_WAKE_LOCK_CAPTURE    = 1U << 4,
} power_wake_lock_t;

/* Any interaction path can call this to reset the inactivity countdown. */
void power_mgr_kick(void);

/* Force states (settings app, alarms ringing, conversation mode). */
void power_mgr_stay_awake(bool on);   /* e.g. while an alarm rings or Gemini talks */
void power_mgr_wake_lock_acquire(power_wake_lock_t lock);
void power_mgr_wake_lock_release(power_wake_lock_t lock);
void power_mgr_sleep_now(void);
void power_mgr_lock_screen_off(void);

/* The lock face uses an AMOLED-safe always-on mode: it may dim, but automatic
 * panel sleep is deferred until the user enters the interactive UI. Explicit
 * sleep requests still win. */
void power_mgr_set_screen_saver_active(bool active);

bool power_mgr_is_awake(void);

/* Persistent user preferences used by Settings and Quick Settings. */
uint8_t  power_mgr_brightness(void);
uint32_t power_mgr_sleep_timeout_seconds(void);
esp_err_t power_mgr_set_brightness(uint8_t percent);
esp_err_t power_mgr_set_sleep_timeout_seconds(uint32_t seconds);

#ifdef __cplusplus
}
#endif
