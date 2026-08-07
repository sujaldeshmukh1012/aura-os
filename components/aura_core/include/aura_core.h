#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "esp_err.h"
#include "esp_event.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- System event bus ----------------
 * All modules communicate through esp_event so apps stay decoupled.
 */
ESP_EVENT_DECLARE_BASE(AURA_EVENT);

typedef enum {
    AURA_EVT_BOOT_DONE,          /* core services up */
    AURA_EVT_WAKE,               /* leaving sleep (double-tap or button) */
    AURA_EVT_PRE_SLEEP,          /* about to sleep: apps flush state now */
    AURA_EVT_BTN_BACK,           /* BOOT short press */
    AURA_EVT_BTN_HOME,           /* BOOT long press */
    AURA_EVT_BTN_MODE_CYCLE,     /* PWR click gesture changed audio mode */
    AURA_EVT_BTN_ASSISTANT,      /* PWR long press: Conversation Mode */
    AURA_EVT_MODE_CHANGED,       /* payload: aura_mode_t */
    AURA_EVT_BATTERY,            /* payload: aura_battery_t */
    AURA_EVT_SETTINGS_CHANGED,   /* timezone/format/power preference changed */
    AURA_EVT_STEPS,              /* payload: today's step count (uint32_t) */
    AURA_EVT_DIGEST_UPDATED,     /* server digest/cache changed */
    AURA_EVT_CAPTURE_CHANGED,    /* capture/outbox status changed */
    AURA_EVT_NOTIFICATIONS_CHANGED, /* ANCS notification cache changed */
    AURA_EVT_MINUTE_TICK,        /* watch face refresh cadence */
    AURA_EVT_ALARM_FIRED,        /* payload: alarm id (int) */
} aura_event_id_t;

typedef struct {
    uint8_t percent;
    bool    charging;
    uint16_t millivolts;
} aura_battery_t;

/* Audio/assistant mode (Phase 4 uses this; the toggle exists from day one
 * so the status bar and PWR button behavior are stable). */
typedef enum {
    AURA_MODE_SILENT = 0,   /* mics hard-off */
    AURA_MODE_CAPTURE,      /* preserve pre-roll + record until stopped */
    AURA_MODE_CONVERSATION, /* live Gemini session (momentary) */
    AURA_MODE_STEALTH,      /* mic -> rolling five-minute SD buffer */
} aura_mode_t;

esp_err_t aura_core_init(void);
esp_err_t aura_post(aura_event_id_t id, const void *data, size_t len);
esp_err_t aura_subscribe(aura_event_id_t id, esp_event_handler_t h, void *arg);

aura_mode_t aura_mode_get(void);
void        aura_mode_set(aura_mode_t m);

/* ---------------- Settings (NVS-backed key/value) ---------------- */
esp_err_t aura_settings_get_i32(const char *key, int32_t *out, int32_t dflt);
esp_err_t aura_settings_set_i32(const char *key, int32_t val);
esp_err_t aura_settings_get_str(const char *key, char *out, size_t len, const char *dflt);
esp_err_t aura_settings_set_str(const char *key, const char *val);
esp_err_t aura_settings_get_blob(const char *key, void *out, size_t *len);
esp_err_t aura_settings_set_blob(const char *key, const void *data, size_t len);

/* ---------------- Time (PCF85063-backed) ----------------
 * On boot we load system time from the RTC chip (battery-backed through the
 * AXP2101), and after every SNTP sync we write it back.
 */
esp_err_t aura_time_init(void);
void      aura_time_now(struct tm *out);
esp_err_t aura_time_set(const struct tm *t);
bool      aura_time_is_24h(void);
int       aura_timezone_offset_minutes(void);
esp_err_t aura_time_set_24h(bool enabled);
esp_err_t aura_timezone_set_offset_minutes(int minutes);

#ifdef __cplusplus
}
#endif
