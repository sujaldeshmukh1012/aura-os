#include "aura_ui.h"
#include "aura_core.h"
#include "aura_config.h"
#include "bsp/esp-bsp.h"
#include "power_mgr.h"
#include "aura_notifications.h"

#include <stdint.h>
#include <stdlib.h>

static lv_obj_t *s_notification_list;
static lv_obj_t *s_notification_title;
static lv_obj_t *s_voice_bars[5];

static void render_notifications(void);

static const char *quick_mode_text(aura_mode_t mode)
{
    if (mode == AURA_MODE_CAPTURE) return "Mode: Capture";
    if (mode == AURA_MODE_STEALTH) return "Mode: Stealth";
    return "Mode: Silent";
}

static void dismiss_notification(lv_event_t *event)
{
    uint32_t uid = (uint32_t)(uintptr_t)lv_event_get_user_data(event);
    lv_obj_add_state(lv_event_get_target(event), LV_STATE_DISABLED);
    (void)aura_notifications_dismiss(uid);
}

static void clear_notifications(lv_event_t *event)
{
    lv_obj_add_state(lv_event_get_target(event), LV_STATE_DISABLED);
    (void)aura_notifications_clear_all();
    render_notifications();
    lv_obj_remove_state(lv_event_get_target(event), LV_STATE_DISABLED);
}

static void notifications_home(lv_event_t *event)
{
    (void)event;
    aura_ui_close_notifications();
}

static void render_notifications(void)
{
    if (!s_notification_list) return;
    aura_notification_t *items = calloc(AURA_NOTIFICATION_MAX, sizeof(*items));
    if (!items) return;
    size_t count = aura_notifications_snapshot(items, AURA_NOTIFICATION_MAX);
    if (s_notification_title)
        lv_label_set_text_fmt(s_notification_title, "Notifications  %u", (unsigned)count);
    lv_obj_clean(s_notification_list);
    if (count == 0) {
        lv_obj_t *label = lv_label_create(s_notification_list);
        lv_label_set_text(label, "No iPhone notifications\nPair with Aura Watch in Bluetooth settings");
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(label, lv_pct(100));
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(label, AURA_COL_MUTED, 0);
    }
    for (size_t i = 0; i < count; ++i) {
        aura_notification_t *item = &items[i];
        lv_obj_t *card = lv_obj_create(s_notification_list);
        lv_obj_set_size(card, lv_pct(100), 184);
        lv_obj_set_style_bg_color(card, lv_color_hex(0x111114), 0);
        lv_obj_set_style_border_width(card, 0, 0);
        lv_obj_set_style_radius(card, 18, 0);
        lv_obj_set_style_pad_all(card, 12, 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *app = lv_label_create(card);
        lv_label_set_text(app, item->app);
        lv_obj_set_style_text_color(app, AURA_COL_ACCENT, 0);
        lv_obj_align(app, LV_ALIGN_TOP_LEFT, 0, 0);
        lv_obj_t *title = lv_label_create(card);
        lv_label_set_text(title, item->title);
        lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
        lv_obj_set_width(title, lv_pct(100));
        lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
        lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 28);
        lv_obj_t *body = lv_label_create(card);
        lv_label_set_text(body, item->body);
        lv_label_set_long_mode(body, LV_LABEL_LONG_DOT);
        lv_obj_set_width(body, lv_pct(100));
        lv_obj_align(body, LV_ALIGN_TOP_LEFT, 0, 62);
        lv_obj_t *button = lv_button_create(card);
        lv_obj_set_size(button, 130, 62);
        lv_obj_align(button, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
        lv_obj_t *text = lv_label_create(button);
        lv_label_set_text(text, "Dismiss"); lv_obj_center(text);
        lv_obj_add_event_cb(button, dismiss_notification, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)item->uid);
    }
    free(items);
}

static void notifications_changed(void *handler, esp_event_base_t base,
                                  int32_t id, void *data)
{
    (void)handler; (void)base; (void)id; (void)data;
    bsp_display_lock(0);
    render_notifications();
    bsp_display_unlock();
}

void panel_notif_build(lv_obj_t *panel)
{
    lv_obj_set_style_bg_color(panel, AURA_COL_BG, 0);
    aura_ui_apply_safe_area(panel);
    lv_obj_set_style_pad_bottom(panel, 50, 0);
    lv_obj_set_style_pad_row(panel, 10, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_t *top = lv_obj_create(panel);
    lv_obj_set_size(top, lv_pct(100), 60);
    lv_obj_set_style_bg_color(top, lv_color_hex(0x0D0E12), 0);
    lv_obj_set_style_border_width(top, 0, 0);
    lv_obj_set_style_radius(top, 16, 0);
    lv_obj_set_style_pad_hor(top, 12, 0);
    lv_obj_set_style_pad_ver(top, 8, 0);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(top, LV_OBJ_FLAG_SCROLLABLE);
    s_notification_title = lv_label_create(top);
    lv_label_set_text(s_notification_title, "Notifications");
    lv_obj_set_style_text_font(s_notification_title, &lv_font_montserrat_24, 0);
    lv_obj_t *clear = lv_button_create(top);
    lv_obj_set_size(clear, 94, 44);
    lv_obj_set_style_bg_color(clear, lv_color_hex(0x28202A), 0);
    lv_obj_set_style_radius(clear, 14, 0);
    lv_obj_t *clear_text = lv_label_create(clear);
    lv_label_set_text(clear_text, "Clear all");
    lv_obj_set_style_text_font(clear_text, &lv_font_montserrat_14, 0);
    lv_obj_center(clear_text);
    lv_obj_add_event_cb(clear, clear_notifications, LV_EVENT_CLICKED, NULL);
    s_notification_list = lv_obj_create(panel);
    lv_obj_set_width(s_notification_list, lv_pct(100));
    lv_obj_set_flex_grow(s_notification_list, 1);
    lv_obj_set_flex_flow(s_notification_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_opa(s_notification_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_notification_list, 0, 0);
    lv_obj_set_style_pad_all(s_notification_list, 0, 0);
    lv_obj_set_style_pad_row(s_notification_list, 10, 0);
    lv_obj_set_scrollbar_mode(s_notification_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_t *bottom = lv_obj_create(panel);
    lv_obj_set_size(bottom, lv_pct(100), 58);
    lv_obj_set_style_bg_color(bottom, lv_color_hex(0x0D0E12), 0);
    lv_obj_set_style_border_width(bottom, 0, 0);
    lv_obj_set_style_radius(bottom, 16, 0);
    lv_obj_set_style_pad_all(bottom, 7, 0);
    lv_obj_clear_flag(bottom, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *home = lv_button_create(bottom);
    lv_obj_set_size(home, lv_pct(100), 44);
    lv_obj_set_style_bg_color(home, lv_color_hex(0x1A1C24), 0);
    lv_obj_set_style_radius(home, 14, 0);
    lv_obj_t *home_text = lv_label_create(home);
    lv_label_set_text(home_text, "Home");
    lv_obj_set_style_text_font(home_text, &lv_font_montserrat_16, 0);
    lv_obj_center(home_text);
    lv_obj_add_event_cb(home, notifications_home, LV_EVENT_CLICKED, NULL);
    render_notifications();
    (void)aura_subscribe(AURA_EVT_NOTIFICATIONS_CHANGED, notifications_changed, NULL);
}

void tile_conversation_build(lv_obj_t *tile)
{
    lv_obj_set_style_bg_color(tile, AURA_COL_BG, 0);
    aura_ui_apply_safe_area(tile);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(tile);
    lv_label_set_text(title, "Talk");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *orb = lv_obj_create(tile);
    lv_obj_set_size(orb, 132, 132);
    lv_obj_set_style_radius(orb, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(orb, lv_color_hex(0x151220), 0);
    lv_obj_set_style_border_width(orb, 2, 0);
    lv_obj_set_style_border_color(orb, lv_color_hex(0xB698F5), 0);
    lv_obj_clear_flag(orb, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(orb, LV_ALIGN_CENTER, 0, -35);
    lv_obj_t *meter = lv_obj_create(orb);
    lv_obj_set_size(meter, 88, 62);
    lv_obj_set_style_bg_opa(meter, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(meter, 0, 0);
    lv_obj_set_style_pad_all(meter, 0, 0);
    lv_obj_clear_flag(meter, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(meter);
    for (int i = 0; i < 5; ++i) {
        s_voice_bars[i] = lv_obj_create(meter);
        lv_obj_set_size(s_voice_bars[i], 9, 10);
        lv_obj_set_style_radius(s_voice_bars[i], 5, 0);
        lv_obj_set_style_bg_color(s_voice_bars[i], lv_color_hex(0xB698F5), 0);
        lv_obj_set_style_border_width(s_voice_bars[i], 0, 0);
        lv_obj_clear_flag(s_voice_bars[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(s_voice_bars[i], LV_ALIGN_BOTTOM_MID, (i - 2) * 17, 0);
    }

    lv_obj_t *status = lv_label_create(tile);
    lv_label_set_text(status, "Voice-reactive Aura control\nLive session foundation arrives in R4");
    lv_obj_set_width(status, 340);
    lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(status, AURA_COL_MUTED, 0);
    lv_obj_align(status, LV_ALIGN_CENTER, 0, 70);
}

void tile_conversation_set_level(uint8_t level)
{
    static const uint8_t scale[] = {55, 80, 100, 80, 55};
    for (int i = 0; i < 5; ++i) {
        if (!s_voice_bars[i]) continue;
        int height = 8 + (level * scale[i] / 255) * 46 / 100;
        lv_obj_set_height(s_voice_bars[i], height);
        lv_obj_align(s_voice_bars[i], LV_ALIGN_BOTTOM_MID, (i - 2) * 17, 0);
    }
}

/* ---------------- Quick settings (functional) ---------------- */

static void qs_mode_cb(lv_event_t *e)
{
    aura_mode_t current = aura_mode_get();
    aura_mode_t m = current == AURA_MODE_SILENT ? AURA_MODE_STEALTH
                    : current == AURA_MODE_STEALTH ? AURA_MODE_CAPTURE
                                                   : AURA_MODE_SILENT;
    aura_mode_set(m);
    lv_label_set_text(lv_obj_get_child(lv_event_get_target(e), 0),
                      quick_mode_text(m));
}

static void qs_bright_cb(lv_event_t *e)
{
    lv_obj_t *s = lv_event_get_target(e);
    (void)power_mgr_set_brightness(lv_slider_get_value(s));
}

static void torch_close_cb(lv_event_t *e);

static void qs_torch_cb(lv_event_t *e)
{
    /* Flashlight: white full-screen modal, tap anywhere to close. */
    lv_obj_t *m = lv_obj_create(lv_layer_top());
    lv_obj_set_size(m, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(m, lv_color_white(), 0);
    bsp_display_brightness_set(100);
    power_mgr_wake_lock_acquire(POWER_WAKE_LOCK_FLASHLIGHT);
    lv_obj_add_flag(m, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(m, torch_close_cb, LV_EVENT_CLICKED, NULL);
}

static void torch_close_cb(lv_event_t *e)
{
    lv_obj_del(lv_event_get_target(e));
    bsp_display_brightness_set(power_mgr_brightness());
    power_mgr_wake_lock_release(POWER_WAKE_LOCK_FLASHLIGHT);
}

void panel_quick_build(lv_obj_t *panel)
{
    lv_obj_set_style_bg_color(panel, AURA_COL_BG, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    aura_ui_apply_safe_area(panel);
    lv_obj_set_style_pad_row(panel, 12, 0);

    lv_obj_t *b = aura_ui_big_button(panel,
                                     quick_mode_text(aura_mode_get()),
                                     lv_color_hex(0x1C1C1F));
    lv_obj_add_event_cb(b, qs_mode_cb, LV_EVENT_CLICKED, NULL);

    b = aura_ui_big_button(panel, "Flashlight",
                           lv_color_hex(0x1C1C1F));
    lv_obj_add_event_cb(b, qs_torch_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *sl = lv_slider_create(panel);
    lv_obj_set_size(sl, lv_pct(100), 40);
    lv_slider_set_range(sl, 5, 100);
    lv_slider_set_value(sl, power_mgr_brightness(), LV_ANIM_OFF);
    lv_obj_add_event_cb(sl, qs_bright_cb, LV_EVENT_VALUE_CHANGED, NULL);
}
