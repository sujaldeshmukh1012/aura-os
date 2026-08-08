#include "aura_ui.h"
#include "aura_core.h"
#include "aura_hw.h"
#include "aura_sync.h"
#include "aura_wifi.h"
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

static lv_obj_t *s_time, *s_time_bold, *s_date, *s_batt, *s_next, *s_wifi, *s_phone;
static lv_obj_t *s_wifi_dot, *s_phone_dot;
static lv_obj_t *s_notif_button, *s_notif_count, *s_unlock_hint;
static int s_last_min = -1;
static uint32_t s_steps;
static void refresh_notification_preview(void);

void tile_watchface_set_visible(bool visible)
{
    /* Reconcile against the notification cache whenever Home becomes visible.
     * This covers removals received while the launcher or another app was up. */
    if (visible && s_notif_count) refresh_notification_preview();
    lv_obj_t *parts[] = {s_time, s_time_bold, s_date, s_batt, s_next, s_wifi,
                         s_phone, s_notif_button, s_unlock_hint};
    for (size_t i = 0; i < sizeof(parts) / sizeof(parts[0]); ++i) {
        if (!parts[i]) continue;
        if (visible) lv_obj_clear_flag(parts[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(parts[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void refresh_connections(void)
{
    bool wifi = aura_wifi_state() == AURA_WIFI_CONNECTED;
    bool phone = aura_notifications_phone_connected();
    lv_label_set_text(s_wifi, wifi ? "Wi-Fi" : "Wi-Fi --");
    lv_obj_set_style_text_color(s_wifi, wifi ? AURA_COL_OK : AURA_COL_MUTED, 0);
    lv_obj_set_style_bg_color(s_wifi_dot, wifi ? AURA_COL_OK : AURA_COL_MUTED, 0);
    lv_label_set_text(s_phone, phone ? "Phone" : "Phone --");
    lv_obj_set_style_text_color(s_phone, phone ? AURA_COL_OK : AURA_COL_MUTED, 0);
    lv_obj_set_style_bg_color(s_phone_dot, phone ? AURA_COL_OK : AURA_COL_MUTED, 0);
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
    char time_text[16];
    if (aura_time_is_24h()) {
        lv_snprintf(time_text, sizeof(time_text), "%d:%02d", t.tm_hour, t.tm_min);
    } else {
        int hour = t.tm_hour % 12;
        if (hour == 0) hour = 12;
        lv_snprintf(time_text, sizeof(time_text), "%d:%02d %s", hour, t.tm_min,
                    t.tm_hour < 12 ? "AM" : "PM");
    }
    lv_label_set_text(s_time, time_text);
    lv_label_set_text(s_time_bold, time_text);
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
    lv_label_set_text_fmt(s_batt, bat->charging ? "%d%% +" : "%d%%", bat->percent);
    bsp_display_unlock();
}

static void on_wake(void *h, esp_event_base_t b, int32_t id, void *d)
{
    s_last_min = -1;           /* force redraw with fresh time */
    bsp_display_lock(0);
    refresh();
    refresh_notification_preview();
    refresh_connections();
    bsp_display_unlock();
}

static void on_settings_changed(void *h, esp_event_base_t b, int32_t id, void *d)
{
    s_last_min = -1;
    bsp_display_lock(0);
    refresh();
    refresh_connections();
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
        lv_label_set_text(s_notif_count, "NO NOTIFICATIONS");
        return;
    }
    lv_label_set_text_fmt(s_notif_count, "%u NOTIFICATION%s",
                          (unsigned)count, count == 1 ? "" : "S");
    refresh_connections();
}

static void on_notifications(void *h, esp_event_base_t b, int32_t id, void *d)
{
    (void)h; (void)b; (void)id; (void)d;
    bsp_display_lock(0);
    refresh_notification_preview();
    bsp_display_unlock();
}

static void notification_button_cb(lv_event_t *event)
{
    (void)event;
    aura_ui_show_notifications();
}

void tile_watchface_build(lv_obj_t *tile)
{
    lv_obj_set_style_bg_color(tile, AURA_COL_BG, 0);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);

    s_time = lv_label_create(tile);
    lv_obj_set_style_text_font(s_time, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_time, lv_color_white(), 0);
    lv_obj_set_style_text_letter_space(s_time, 3, 0);
    lv_obj_align(s_time, LV_ALIGN_CENTER, -1, -72);

    /* A one-pixel duplicate produces a materially heavier numeral face without
     * shipping another large font table into flash. */
    s_time_bold = lv_label_create(tile);
    lv_obj_set_style_text_font(s_time_bold, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_time_bold, lv_color_white(), 0);
    lv_obj_set_style_text_letter_space(s_time_bold, 3, 0);
    lv_obj_align(s_time_bold, LV_ALIGN_CENTER, 1, -72);

    s_date = lv_label_create(tile);
    lv_obj_set_style_text_font(s_date, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_date, AURA_COL_MUTED, 0);
    lv_obj_set_width(s_date, 330);
    lv_obj_set_style_text_align(s_date, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_date, LV_ALIGN_CENTER, 0, -18);

    s_next = lv_label_create(tile);                     /* next event slot */
    s_steps = aura_hw_steps();
    lv_label_set_text_fmt(s_next, "%lu steps", (unsigned long)s_steps);
    lv_obj_set_width(s_next, 340);
    lv_obj_set_style_text_align(s_next, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_next, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(s_next, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_next, AURA_COL_ACCENT, 0);
    lv_obj_align(s_next, LV_ALIGN_CENTER, 0, 35);

    s_notif_button = lv_button_create(tile);
    lv_obj_set_size(s_notif_button, 230, 48);
    lv_obj_set_style_bg_color(s_notif_button, lv_color_hex(0x101117), 0);
    lv_obj_set_style_border_width(s_notif_button, 1, 0);
    lv_obj_set_style_border_color(s_notif_button, lv_color_hex(0x242733), 0);
    lv_obj_set_style_radius(s_notif_button, 16, 0);
    lv_obj_set_style_shadow_width(s_notif_button, 0, 0);
    lv_obj_align(s_notif_button, LV_ALIGN_CENTER, 0, 88);
    lv_obj_add_event_cb(s_notif_button, notification_button_cb, LV_EVENT_CLICKED, NULL);
    s_notif_count = lv_label_create(s_notif_button);
    lv_obj_set_style_text_font(s_notif_count, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_notif_count, AURA_COL_ACCENT, 0);
    lv_obj_center(s_notif_count);

    s_unlock_hint = lv_label_create(tile);
    lv_obj_set_style_text_font(s_unlock_hint, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_unlock_hint, AURA_COL_MUTED, 0);
    lv_label_set_text(s_unlock_hint, "Tap to unlock");
    lv_obj_set_width(s_unlock_hint, 350);
    lv_obj_set_style_text_align(s_unlock_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_unlock_hint, LV_ALIGN_BOTTOM_MID, 0, -55);

    s_batt = lv_label_create(tile);
    lv_label_set_text(s_batt, "--%");
    lv_obj_set_style_text_font(s_batt, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_batt, AURA_COL_MUTED, 0);
    lv_obj_align(s_batt, LV_ALIGN_TOP_RIGHT, -AURA_SCREEN_SAFE_SIDE,
                 AURA_SCREEN_SAFE_TOP + 6);

    s_wifi = lv_label_create(tile);
    lv_obj_set_style_text_font(s_wifi, &lv_font_montserrat_14, 0);
    lv_obj_align(s_wifi, LV_ALIGN_TOP_LEFT, AURA_SCREEN_SAFE_SIDE + 12,
                 AURA_SCREEN_SAFE_TOP + 7);

    s_wifi_dot = lv_obj_create(tile);
    lv_obj_set_size(s_wifi_dot, 7, 7);
    lv_obj_set_style_radius(s_wifi_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_wifi_dot, 0, 0);
    lv_obj_clear_flag(s_wifi_dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(s_wifi_dot, LV_ALIGN_TOP_LEFT, AURA_SCREEN_SAFE_SIDE,
                 AURA_SCREEN_SAFE_TOP + 12);

    s_phone = lv_label_create(tile);
    lv_obj_set_style_text_font(s_phone, &lv_font_montserrat_14, 0);
    lv_obj_align(s_phone, LV_ALIGN_TOP_LEFT, AURA_SCREEN_SAFE_SIDE + 94,
                 AURA_SCREEN_SAFE_TOP + 7);

    s_phone_dot = lv_obj_create(tile);
    lv_obj_set_size(s_phone_dot, 7, 7);
    lv_obj_set_style_radius(s_phone_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_phone_dot, 0, 0);
    lv_obj_clear_flag(s_phone_dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(s_phone_dot, LV_ALIGN_TOP_LEFT, AURA_SCREEN_SAFE_SIDE + 82,
                 AURA_SCREEN_SAFE_TOP + 12);

    refresh();
    refresh_notification_preview();
    refresh_connections();
    aura_subscribe(AURA_EVT_MINUTE_TICK, on_tick, NULL);
    aura_subscribe(AURA_EVT_BATTERY, on_battery, NULL);
    aura_subscribe(AURA_EVT_WAKE, on_wake, NULL);
    aura_subscribe(AURA_EVT_SETTINGS_CHANGED, on_settings_changed, NULL);
    aura_subscribe(AURA_EVT_STEPS, on_steps, NULL);
    aura_subscribe(AURA_EVT_DIGEST_UPDATED, on_digest, NULL);
    aura_subscribe(AURA_EVT_NOTIFICATIONS_CHANGED, on_notifications, NULL);
}
