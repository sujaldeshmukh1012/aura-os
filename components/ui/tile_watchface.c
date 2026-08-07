#include "aura_ui.h"
#include "aura_core.h"
#include "aura_hw.h"
#include "aura_sync.h"
#include "aura_notifications.h"
#include "esp_log.h"
#include "bsp/esp-bsp.h"
#include <stdio.h>
#include <stdlib.h>

/* Minimal, AMOLED-friendly face:
 *   12:47          <- huge
 *   Sat, Aug 1     <- muted
 *   [mode dot] [battery]
 * Later: next calendar event line + step count slot are already reserved.
 */

static lv_obj_t *s_time, *s_date, *s_batt, *s_next;
static lv_obj_t *s_notif_count, *s_unlock_hint;
static int s_last_min = -1;
static uint32_t s_steps;
static void refresh_notification_preview(void);

void tile_watchface_set_visible(bool visible)
{
    /* Reconcile against the notification cache whenever Home becomes visible.
     * This covers removals received while the launcher or another app was up. */
    if (visible && s_notif_count) refresh_notification_preview();
    lv_obj_t *parts[] = {s_time, s_date, s_batt, s_next,
                         s_notif_count, s_unlock_hint};
    for (size_t i = 0; i < sizeof(parts) / sizeof(parts[0]); ++i) {
        if (!parts[i]) continue;
        if (visible) lv_obj_clear_flag(parts[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(parts[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void refresh(void)
{
    struct tm t;
    aura_time_now(&t);
    if (t.tm_min == s_last_min) return;
    s_last_min = t.tm_min;

    static const char *wd[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    static const char *mo[] = {"Jan","Feb","Mar","Apr","May","Jun",
                               "Jul","Aug","Sep","Oct","Nov","Dec"};
    if (aura_time_is_24h()) {
        lv_label_set_text_fmt(s_time, "%d:%02d", t.tm_hour, t.tm_min);
    } else {
        int hour = t.tm_hour % 12;
        if (hour == 0) hour = 12;
        lv_label_set_text_fmt(s_time, "%d:%02d %s", hour, t.tm_min,
                              t.tm_hour < 12 ? "AM" : "PM");
    }
    lv_label_set_text_fmt(s_date, "%s, %s %d", wd[t.tm_wday], mo[t.tm_mon], t.tm_mday);
}

static void on_tick(void *h, esp_event_base_t b, int32_t id, void *d)
{
    bsp_display_lock(0);
    refresh();
    bsp_display_unlock();
}

static void on_battery(void *h, esp_event_base_t b, int32_t id, void *d)
{
    aura_battery_t *bat = (aura_battery_t *)d;
    bsp_display_lock(0);
    lv_label_set_text_fmt(s_batt, "%s %d%%",
        bat->charging ? LV_SYMBOL_CHARGE : LV_SYMBOL_BATTERY_3, bat->percent);
    bsp_display_unlock();
}

static void on_wake(void *h, esp_event_base_t b, int32_t id, void *d)
{
    s_last_min = -1;           /* force redraw with fresh time */
    bsp_display_lock(0);
    refresh();
    refresh_notification_preview();
    bsp_display_unlock();
}

static void on_settings_changed(void *h, esp_event_base_t b, int32_t id, void *d)
{
    s_last_min = -1;
    bsp_display_lock(0);
    refresh();
    bsp_display_unlock();
}

static void on_steps(void *h, esp_event_base_t b, int32_t id, void *d)
{
    s_steps = *(uint32_t *)d;
    bsp_display_lock(0);
    aura_sync_snapshot_t *snapshot = malloc(sizeof(*snapshot));
    if (!snapshot) {
        lv_label_set_text_fmt(s_next, "%lu steps", (unsigned long)s_steps);
        bsp_display_unlock();
        return;
    }
    aura_sync_snapshot(snapshot);
    if (snapshot->event_count) {
        struct tm local;
        localtime_r(&snapshot->events[0].start_epoch, &local);
        char at[16];
        strftime(at, sizeof(at), "%I:%M %p", &local);
        lv_label_set_text_fmt(s_next, "%s  %s\n%lu steps", at,
                              snapshot->events[0].title, (unsigned long)s_steps);
    } else {
        lv_label_set_text_fmt(s_next, "%lu steps", (unsigned long)s_steps);
    }
    free(snapshot);
    bsp_display_unlock();
}

static void on_digest(void *h, esp_event_base_t b, int32_t id, void *d)
{
    on_steps(h, b, id, &s_steps);
}

static void refresh_notification_preview(void)
{
    size_t count = aura_notifications_count();
    if (count == 0) {
        lv_label_set_text(s_notif_count, LV_SYMBOL_BELL "  No notifications");
        return;
    }
    lv_label_set_text_fmt(s_notif_count, LV_SYMBOL_BELL "  %u notification%s",
                          (unsigned)count, count == 1 ? "" : "s");
}

static void on_notifications(void *h, esp_event_base_t b, int32_t id, void *d)
{
    (void)h; (void)b; (void)id; (void)d;
    bsp_display_lock(0);
    refresh_notification_preview();
    bsp_display_unlock();
}

void tile_watchface_build(lv_obj_t *tile)
{
    lv_obj_set_style_bg_color(tile, AURA_COL_BG, 0);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);

    s_time = lv_label_create(tile);
    lv_obj_set_style_text_font(s_time, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_time, lv_color_white(), 0);
    lv_obj_align(s_time, LV_ALIGN_CENTER, 0, -75);

    s_date = lv_label_create(tile);
    lv_obj_set_style_text_font(s_date, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_date, AURA_COL_MUTED, 0);
    lv_obj_set_width(s_date, 220);
    lv_obj_align(s_date, LV_ALIGN_TOP_LEFT, AURA_SCREEN_SAFE_SIDE,
                 AURA_SCREEN_SAFE_TOP);

    s_next = lv_label_create(tile);                     /* next event slot */
    s_steps = aura_hw_steps();
    lv_label_set_text_fmt(s_next, "%lu steps", (unsigned long)s_steps);
    lv_obj_set_width(s_next, 340);
    lv_obj_set_style_text_align(s_next, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_next, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(s_next, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_next, AURA_COL_ACCENT, 0);
    lv_obj_align(s_next, LV_ALIGN_CENTER, 0, 15);

    s_notif_count = lv_label_create(tile);
    lv_obj_set_style_text_font(s_notif_count, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_notif_count, AURA_COL_ACCENT, 0);
    lv_obj_align(s_notif_count, LV_ALIGN_CENTER, 0, 82);

    s_unlock_hint = lv_label_create(tile);
    lv_label_set_text(s_unlock_hint, "Tap to unlock");
    lv_obj_set_style_text_font(s_unlock_hint, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_unlock_hint, AURA_COL_MUTED, 0);
    lv_label_set_text(s_unlock_hint, "Tap to unlock  ·  Swipe down for notifications");
    lv_obj_set_width(s_unlock_hint, 350);
    lv_obj_set_style_text_align(s_unlock_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_unlock_hint, LV_ALIGN_BOTTOM_MID, 0, -55);

    s_batt = lv_label_create(tile);
    lv_label_set_text(s_batt, LV_SYMBOL_BATTERY_3 " --%");
    lv_obj_set_style_text_font(s_batt, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_batt, AURA_COL_MUTED, 0);
    lv_obj_align(s_batt, LV_ALIGN_TOP_RIGHT, -AURA_SCREEN_SAFE_SIDE,
                 AURA_SCREEN_SAFE_TOP + 6);

    refresh();
    refresh_notification_preview();
    aura_subscribe(AURA_EVT_MINUTE_TICK, on_tick, NULL);
    aura_subscribe(AURA_EVT_BATTERY, on_battery, NULL);
    aura_subscribe(AURA_EVT_WAKE, on_wake, NULL);
    aura_subscribe(AURA_EVT_SETTINGS_CHANGED, on_settings_changed, NULL);
    aura_subscribe(AURA_EVT_STEPS, on_steps, NULL);
    aura_subscribe(AURA_EVT_DIGEST_UPDATED, on_digest, NULL);
    aura_subscribe(AURA_EVT_NOTIFICATIONS_CHANGED, on_notifications, NULL);
}
