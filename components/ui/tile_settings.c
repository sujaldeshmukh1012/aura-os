#include "aura_ui.h"

#include "aura_core.h"
#include "aura_wifi.h"
#include "aura_sync.h"
#include "bsp/esp-bsp.h"
#include "power_mgr.h"

static lv_obj_t *s_brightness_value;
static lv_obj_t *s_clock_button_label;
static lv_obj_t *s_sleep_button_label;
static lv_obj_t *s_wifi_button_label;
static lv_obj_t *s_network_meta;
static lv_obj_t *s_lock_reset_label;
static lv_obj_t *s_notification_sound_label;

static void refresh_network_metadata(void)
{
    if (!s_network_meta) return;
    lv_label_set_text_fmt(s_network_meta,
        "Wi-Fi  %s\nIP  %s\nTime  %s\nAura  %s",
        aura_wifi_ssid(), aura_wifi_ip(),
        aura_wifi_time_is_synced() ? "current" : "waiting",
        aura_sync_status_text());
}

static void brightness_cb(lv_event_t *e)
{
    int value = lv_slider_get_value(lv_event_get_target(e));
    lv_label_set_text_fmt(s_brightness_value, "%d%%", value);
    if (lv_event_get_code(e) == LV_EVENT_RELEASED)
        (void)power_mgr_set_brightness(value);
    else
        bsp_display_brightness_set(value);
}

static void clock_format_cb(lv_event_t *e)
{
    bool use_24h = !aura_time_is_24h();
    (void)aura_time_set_24h(use_24h);
    lv_label_set_text(s_clock_button_label, use_24h ? "Clock: 24 hour" : "Clock: 12 hour");
}

static void sleep_timeout_cb(lv_event_t *e)
{
    uint32_t current = power_mgr_sleep_timeout_seconds();
    uint32_t next = current < 30 ? 30 : current < 60 ? 60 : current < 120 ? 120 : 15;
    (void)power_mgr_set_sleep_timeout_seconds(next);
    lv_label_set_text_fmt(s_sleep_button_label, "Screen off: %lu sec", (unsigned long)next);
}

static void wifi_setup_cb(lv_event_t *e)
{
    if (aura_wifi_setup_hotspot_active()) (void)aura_wifi_stop_provisioning();
    else (void)aura_wifi_start_provisioning();
    lv_label_set_text(s_wifi_button_label, aura_wifi_status_text());
    refresh_network_metadata();
}

static void lock_reset_cb(lv_event_t *e)
{
    lv_obj_t *button = lv_event_get_target(e);
    if (aura_ui_reset_passcode() == ESP_OK) {
        lv_label_set_text(s_lock_reset_label, "Passcode cleared - lock to enroll");
        lv_obj_add_state(button, LV_STATE_DISABLED);
    } else {
        lv_label_set_text(s_lock_reset_label, "Could not clear passcode");
    }
}

static bool notification_sound_enabled(void)
{
    int32_t enabled = 1;
    (void)aura_settings_get_i32("notif_sound", &enabled, 1);
    return enabled != 0;
}

static void notification_sound_cb(lv_event_t *e)
{
    bool enabled = !notification_sound_enabled();
    (void)aura_settings_set_i32("notif_sound", enabled ? 1 : 0);
    lv_label_set_text(s_notification_sound_label,
                      enabled ? "Notification sound: On"
                              : "Notification sound: Off");
}

static void settings_event(void *h, esp_event_base_t b, int32_t id, void *d)
{
    bsp_display_lock(0);
    if (s_wifi_button_label) lv_label_set_text(s_wifi_button_label, aura_wifi_status_text());
    refresh_network_metadata();
    bsp_display_unlock();
}

static lv_obj_t *section_label(lv_obj_t *parent, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, AURA_COL_MUTED, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    return label;
}

static lv_obj_t *compact_button(lv_obj_t *parent, const char *text,
                                lv_color_t color)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, lv_pct(100), 56);
    lv_obj_set_style_bg_color(button, color, 0);
    lv_obj_set_style_radius(button, 16, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    lv_obj_center(label);
    return button;
}

void tile_settings_build(lv_obj_t *tile)
{
    lv_obj_set_style_bg_color(tile, AURA_COL_BG, 0);
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
    aura_ui_apply_safe_area(tile);
    lv_obj_set_style_pad_top(tile, 34, 0);
    lv_obj_set_style_pad_bottom(tile, 46, 0);
    lv_obj_set_style_pad_row(tile, 8, 0);
    lv_obj_set_scrollbar_mode(tile, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(tile, LV_DIR_VER);

    lv_obj_t *title = lv_label_create(tile);
    lv_label_set_text(title, "Settings");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);

    section_label(tile, "DISPLAY");
    s_brightness_value = section_label(tile, "Brightness");
    lv_label_set_text_fmt(s_brightness_value, "Brightness  %d%%", power_mgr_brightness());
    lv_obj_t *slider = lv_slider_create(tile);
    lv_obj_set_size(slider, lv_pct(100), 24);
    lv_slider_set_range(slider, 5, 100);
    lv_slider_set_value(slider, power_mgr_brightness(), LV_ANIM_OFF);
    lv_obj_add_event_cb(slider, brightness_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(slider, brightness_cb, LV_EVENT_RELEASED, NULL);

    lv_obj_t *button = compact_button(tile,
        aura_time_is_24h() ? "Clock: 24 hour" : "Clock: 12 hour", lv_color_hex(0x1C1C1F));
    s_clock_button_label = lv_obj_get_child(button, 0);
    lv_obj_add_event_cb(button, clock_format_cb, LV_EVENT_CLICKED, NULL);

    char sleep_text[32];
    lv_snprintf(sleep_text, sizeof(sleep_text), "Screen off: %lu sec",
                (unsigned long)power_mgr_sleep_timeout_seconds());
    button = compact_button(tile, sleep_text, lv_color_hex(0x1C1C1F));
    s_sleep_button_label = lv_obj_get_child(button, 0);
    lv_obj_add_event_cb(button, sleep_timeout_cb, LV_EVENT_CLICKED, NULL);

    section_label(tile, "CONNECTIONS");
    lv_obj_t *meta = lv_obj_create(tile);
    lv_obj_set_size(meta, lv_pct(100), 112);
    lv_obj_set_style_bg_color(meta, lv_color_hex(0x0D0E12), 0);
    lv_obj_set_style_border_width(meta, 1, 0);
    lv_obj_set_style_border_color(meta, lv_color_hex(0x1B1D24), 0);
    lv_obj_set_style_radius(meta, 16, 0);
    lv_obj_set_style_pad_all(meta, 12, 0);
    lv_obj_clear_flag(meta, LV_OBJ_FLAG_SCROLLABLE);
    s_network_meta = lv_label_create(meta);
    lv_obj_set_width(s_network_meta, lv_pct(100));
    lv_label_set_long_mode(s_network_meta, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_network_meta, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_network_meta, AURA_COL_TEXT, 0);
    refresh_network_metadata();

    button = compact_button(tile, aura_wifi_status_text(), lv_color_hex(0x1C1C1F));
    s_wifi_button_label = lv_obj_get_child(button, 0);
    lv_obj_add_event_cb(button, wifi_setup_cb, LV_EVENT_CLICKED, NULL);

    section_label(tile, "ALERTS");
    button = compact_button(tile,
        notification_sound_enabled() ? "Notification sound: On"
                                     : "Notification sound: Off",
        lv_color_hex(0x1C1C1F));
    s_notification_sound_label = lv_obj_get_child(button, 0);
    lv_obj_add_event_cb(button, notification_sound_cb, LV_EVENT_CLICKED, NULL);

    section_label(tile, "SECURITY");
    button = compact_button(tile, "Reset passcode", lv_color_hex(0x352027));
    s_lock_reset_label = lv_obj_get_child(button, 0);
    lv_obj_add_event_cb(button, lock_reset_cb, LV_EVENT_CLICKED, NULL);
    (void)aura_subscribe(AURA_EVT_SETTINGS_CHANGED, settings_event, NULL);
}
