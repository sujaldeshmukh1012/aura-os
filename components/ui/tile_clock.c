#include "aura_ui.h"

#include <stdint.h>

#include "aura_config.h"
#include "aura_core.h"
#include "esp_timer.h"

/* A single-purpose countdown timer.  It deliberately keeps its state in RAM:
 * light sleep preserves esp_timer, while a reboot cancels the countdown. */

typedef enum {
    TIMER_IDLE,
    TIMER_RUNNING,
    TIMER_PAUSED,
} timer_state_t;

static lv_obj_t *s_time_label;
static lv_obj_t *s_status_label;
static lv_obj_t *s_primary_label;
static timer_state_t s_state;
static int s_set_minutes = 5;
static int64_t s_remaining_us = 5LL * 60LL * 1000000LL;
static int64_t s_end_us;

static void render_time(int64_t remaining_us)
{
    int total_seconds = (int)((remaining_us + 999999) / 1000000);
    int hours = total_seconds / 3600;
    int minutes = (total_seconds / 60) % 60;
    int seconds = total_seconds % 60;
    if (hours > 0)
        lv_label_set_text_fmt(s_time_label, "%d:%02d:%02d", hours, minutes, seconds);
    else
        lv_label_set_text_fmt(s_time_label, "%02d:%02d", minutes, seconds);
}

static void render_state(void)
{
    render_time(s_remaining_us);
    switch (s_state) {
    case TIMER_RUNNING:
        lv_label_set_text(s_status_label, "COUNTING DOWN");
        lv_label_set_text(s_primary_label, "Pause");
        break;
    case TIMER_PAUSED:
        lv_label_set_text(s_status_label, "PAUSED");
        lv_label_set_text(s_primary_label, "Resume");
        break;
    default:
        lv_label_set_text(s_status_label, "READY");
        lv_label_set_text(s_primary_label, "Start");
        break;
    }
}

static void timer_tick(lv_timer_t *timer)
{
    (void)timer;
    if (s_state != TIMER_RUNNING) return;

    s_remaining_us = s_end_us - esp_timer_get_time();
    if (s_remaining_us <= 0) {
        s_remaining_us = 0;
        s_state = TIMER_IDLE;
        render_time(0);
        lv_label_set_text(s_status_label, "FINISHED");
        lv_label_set_text(s_primary_label, "Start again");
        (void)aura_post(AURA_EVT_ALARM_FIRED, &(int){-1}, sizeof(int));
        return;
    }
    render_time(s_remaining_us);
}

static void set_duration(int minutes)
{
    s_set_minutes = LV_CLAMP(1, minutes, 120);
    s_remaining_us = (int64_t)s_set_minutes * 60LL * 1000000LL;
    s_state = TIMER_IDLE;
    render_state();
}

static void adjust_cb(lv_event_t *event)
{
    int delta = (int)(intptr_t)lv_event_get_user_data(event);
    set_duration(s_set_minutes + delta);
}

static void preset_cb(lv_event_t *event)
{
    set_duration((int)(intptr_t)lv_event_get_user_data(event));
}

static void primary_cb(lv_event_t *event)
{
    (void)event;
    if (s_state == TIMER_RUNNING) {
        s_remaining_us = s_end_us - esp_timer_get_time();
        if (s_remaining_us < 0) s_remaining_us = 0;
        s_state = TIMER_PAUSED;
    } else {
        if (s_remaining_us <= 0)
            s_remaining_us = (int64_t)s_set_minutes * 60LL * 1000000LL;
        s_end_us = esp_timer_get_time() + s_remaining_us;
        s_state = TIMER_RUNNING;
    }
    render_state();
}

static void reset_cb(lv_event_t *event)
{
    (void)event;
    set_duration(s_set_minutes);
}

static lv_obj_t *compact_button(lv_obj_t *parent, const char *text,
                                lv_color_t color, int user_value,
                                lv_event_cb_t callback)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_flex_grow(button, 1);
    lv_obj_set_height(button, AURA_MIN_TOUCH_TARGET);
    lv_obj_set_style_bg_color(button, color, 0);
    lv_obj_set_style_radius(button, 18, 0);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    lv_obj_center(label);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED,
                        (void *)(intptr_t)user_value);
    return button;
}

void tile_clock_build(lv_obj_t *tile)
{
    lv_obj_set_style_bg_color(tile, AURA_COL_BG, 0);
    aura_ui_apply_safe_area(tile);
    lv_obj_set_style_pad_row(tile, 10, 0);
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(tile);
    lv_label_set_text(title, "Timer");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);

    s_status_label = lv_label_create(tile);
    lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_status_label, AURA_COL_ACCENT, 0);

    lv_obj_t *time_card = lv_obj_create(tile);
    lv_obj_set_size(time_card, lv_pct(100), 140);
    lv_obj_set_style_bg_color(time_card, lv_color_hex(0x111114), 0);
    lv_obj_set_style_border_width(time_card, 0, 0);
    lv_obj_set_style_radius(time_card, 24, 0);
    lv_obj_clear_flag(time_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *minus = lv_button_create(time_card);
    lv_obj_set_size(minus, 64, AURA_MIN_TOUCH_TARGET);
    lv_obj_set_style_bg_color(minus, lv_color_hex(0x242428), 0);
    lv_obj_set_style_radius(minus, 20, 0);
    lv_obj_align(minus, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_t *label = lv_label_create(minus);
    lv_label_set_text(label, "-");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_28, 0);
    lv_obj_center(label);
    lv_obj_add_event_cb(minus, adjust_cb, LV_EVENT_CLICKED, (void *)(intptr_t)-1);

    s_time_label = lv_label_create(time_card);
    lv_obj_set_style_text_font(s_time_label, &lv_font_montserrat_48, 0);
    lv_obj_align(s_time_label, LV_ALIGN_CENTER, 0, -5);

    lv_obj_t *plus = lv_button_create(time_card);
    lv_obj_set_size(plus, 64, AURA_MIN_TOUCH_TARGET);
    lv_obj_set_style_bg_color(plus, lv_color_hex(0x242428), 0);
    lv_obj_set_style_radius(plus, 20, 0);
    lv_obj_align(plus, LV_ALIGN_RIGHT_MID, -10, 0);
    label = lv_label_create(plus);
    lv_label_set_text(label, "+");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_28, 0);
    lv_obj_center(label);
    lv_obj_add_event_cb(plus, adjust_cb, LV_EVENT_CLICKED, (void *)(intptr_t)1);

    lv_obj_t *hint = lv_label_create(time_card);
    lv_label_set_text(hint, "MINUTES");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(hint, AURA_COL_MUTED, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -10);

    lv_obj_t *presets = lv_obj_create(tile);
    lv_obj_set_size(presets, lv_pct(100), AURA_MIN_TOUCH_TARGET);
    lv_obj_set_flex_flow(presets, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_bg_opa(presets, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(presets, 0, 0);
    lv_obj_set_style_pad_all(presets, 0, 0);
    lv_obj_set_style_pad_column(presets, 8, 0);
    lv_obj_clear_flag(presets, LV_OBJ_FLAG_SCROLLABLE);

    static const int values[] = {1, 5, 10, 25};
    static const char *names[] = {"1m", "5m", "10m", "25m"};
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
        compact_button(presets, names[i], lv_color_hex(0x1C1C1F),
                       values[i], preset_cb);

    lv_obj_t *actions = lv_obj_create(tile);
    lv_obj_set_size(actions, lv_pct(100), AURA_MIN_TOUCH_TARGET);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_bg_opa(actions, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(actions, 0, 0);
    lv_obj_set_style_pad_all(actions, 0, 0);
    lv_obj_set_style_pad_column(actions, 10, 0);
    lv_obj_clear_flag(actions, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *primary = compact_button(actions, "Start",
                                       lv_color_hex(0x0B4968), 0, primary_cb);
    s_primary_label = lv_obj_get_child(primary, 0);
    compact_button(actions, "Reset",
                   lv_color_hex(0x1C1C1F), 0, reset_cb);

    render_state();
    lv_timer_create(timer_tick, 200, NULL);
}
