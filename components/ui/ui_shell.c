#include "aura_ui.h"
#include "aura_core.h"
#include "aura_config.h"
#include "aura_sync.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "esp_random.h"
#include "psa/crypto.h"
#include "power_mgr.h"

static const char *TAG = "ui";

typedef enum {
    UI_LOCKED,
    UI_UNLOCK,
    UI_LAUNCHER,
    UI_APP,
} ui_state_t;

#define PIN_MAGIC 0x41555049u
#define PIN_LENGTH 4

typedef struct {
    uint32_t magic;
    uint8_t salt[16];
    uint8_t digest[32];
} pin_record_t;

static lv_obj_t *s_root;
static lv_obj_t *s_lock_screen;
static lv_obj_t *s_unlock_screen;
static lv_obj_t *s_launcher;
static lv_obj_t *s_apps[6];
static lv_obj_t *s_current_app;
static lv_obj_t *s_pin_hint;
static lv_obj_t *s_pin_value;
static char s_pin[PIN_LENGTH + 1];
static uint8_t s_pin_len;
static char s_enroll_pin[PIN_LENGTH + 1];
static bool s_enroll_pending;
static pin_record_t s_pin_record;
static bool s_has_pin;
static ui_state_t s_state = UI_LOCKED;
static bool s_edge_tracking;
static bool s_edge_from_left;
static lv_point_t s_edge_start;

/* ---------------- shared widgets ---------------- */

void aura_ui_apply_safe_area(lv_obj_t *obj)
{
    lv_obj_set_style_pad_left(obj, AURA_SCREEN_SAFE_SIDE, 0);
    lv_obj_set_style_pad_right(obj, AURA_SCREEN_SAFE_SIDE, 0);
    lv_obj_set_style_pad_top(obj, AURA_SCREEN_SAFE_TOP, 0);
    lv_obj_set_style_pad_bottom(obj, AURA_SCREEN_SAFE_BOTTOM, 0);
}

lv_obj_t *aura_ui_big_button(lv_obj_t *parent, const char *txt, lv_color_t col)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, lv_pct(100), AURA_MIN_TOUCH_TARGET);
    lv_obj_set_style_bg_color(btn, col, 0);
    lv_obj_set_style_radius(btn, 20, 0);
    lv_obj_t *l = lv_label_create(btn);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_28, 0);
    lv_obj_center(l);
    return btn;
}

lv_obj_t *aura_ui_list_row(lv_obj_t *parent, const char *title, const char *sub)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, lv_pct(100), sub ? 96 : AURA_MIN_TOUCH_TARGET);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x111114), 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 18, 0);
    lv_obj_set_style_pad_all(row, 14, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *t = lv_label_create(row);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(t, AURA_COL_TEXT, 0);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_label_set_long_mode(t, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(t, lv_pct(100));

    if (sub) {
        lv_obj_t *s = lv_label_create(row);
        lv_label_set_text(s, sub);
        lv_obj_set_style_text_font(s, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(s, AURA_COL_MUTED, 0);
        lv_obj_align(s, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    }
    return row;
}

static void style_screen(lv_obj_t *screen)
{
    lv_obj_set_size(screen, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(screen, AURA_COL_BG, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(screen, AURA_COL_TEXT, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_radius(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
}

static void show_only(lv_obj_t *screen)
{
    lv_obj_t *screens[] = {s_lock_screen, s_unlock_screen, s_launcher,
                           s_apps[0], s_apps[1], s_apps[2], s_apps[3],
                           s_apps[4], s_apps[5]};
    for (size_t i = 0; i < sizeof(screens) / sizeof(screens[0]); ++i) {
        if (!screens[i]) continue;
        if (screens[i] == screen) lv_obj_clear_flag(screens[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(screens[i], LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_move_foreground(screen);
    lv_obj_invalidate(s_root);
}

static void show_lock(void)
{
    s_state = UI_LOCKED;
    s_current_app = NULL;
    s_pin_len = 0;
    s_enroll_pending = false;
    tile_watchface_set_visible(true);
    power_mgr_set_screen_saver_active(true);
    show_only(s_lock_screen);
}

static void show_launcher(void)
{
    s_state = UI_LAUNCHER;
    s_current_app = NULL;
    power_mgr_set_screen_saver_active(false);
    show_only(s_launcher);
}

static void navigate_back(void)
{
    if (s_state == UI_APP) show_launcher();
    else if (s_state != UI_LOCKED) show_lock();
}

void aura_ui_go_home(void)
{
    /* Home is deliberately the private, glanceable watch face. */
    show_lock();
}

/* ---------------- numeric PIN lock ---------------- */

static void pin_hash(const uint8_t salt[16], const char pin[PIN_LENGTH],
                     uint8_t out[32])
{
    uint8_t input[16 + PIN_LENGTH];
    size_t output_len = 0;
    memcpy(input, salt, 16);
    memcpy(input + 16, pin, PIN_LENGTH);
    if (psa_crypto_init() != PSA_SUCCESS ||
        psa_hash_compute(PSA_ALG_SHA_256, input, sizeof(input),
                         out, 32, &output_len) != PSA_SUCCESS ||
        output_len != 32) {
        /* A crypto failure must never result in an accidentally accepted PIN.
         * A zero digest will not match a valid enrolled record. */
        memset(out, 0, 32);
    }
}

static void pin_load(void)
{
    size_t len = sizeof(s_pin_record);
    s_has_pin = aura_settings_get_blob("lock_pin", &s_pin_record, &len) == ESP_OK &&
                len == sizeof(s_pin_record) && s_pin_record.magic == PIN_MAGIC;
}

esp_err_t aura_ui_reset_passcode(void)
{
    pin_record_t empty = {0};
    esp_err_t err = aura_settings_set_blob("lock_pin", &empty, sizeof(empty));
    if (err == ESP_OK) {
        memset(&s_pin_record, 0, sizeof(s_pin_record));
        s_has_pin = false;
        s_enroll_pending = false;
    }
    return err;
}

static void pin_render_value(void)
{
    char display[PIN_LENGTH * 2 + 1];
    size_t pos = 0;
    for (uint8_t i = 0; i < PIN_LENGTH; ++i) {
        display[pos++] = i < s_pin_len ? '*' : '-';
        if (i + 1 < PIN_LENGTH) display[pos++] = ' ';
    }
    display[pos] = '\0';
    lv_label_set_text(s_pin_value, display);
}

static void pin_clear(void)
{
    memset(s_pin, 0, sizeof(s_pin));
    s_pin_len = 0;
    pin_render_value();
}

static void show_unlock(void)
{
    s_state = UI_UNLOCK;
    power_mgr_set_screen_saver_active(false);
    pin_clear();
    if (s_has_pin)
        lv_label_set_text(s_pin_hint, "Enter your 4-digit passcode");
    else if (s_enroll_pending)
        lv_label_set_text(s_pin_hint, "Enter it again to confirm");
    else
        lv_label_set_text(s_pin_hint, "Create a 4-digit passcode");
    show_only(s_unlock_screen);
}

static bool pin_matches_saved(void)
{
    uint8_t digest[32];
    pin_hash(s_pin_record.salt, s_pin, digest);
    return memcmp(digest, s_pin_record.digest, sizeof(digest)) == 0;
}

static void pin_complete(void)
{
    if (s_has_pin) {
        if (pin_matches_saved()) {
            show_launcher();
        } else {
            lv_label_set_text(s_pin_hint, "Wrong passcode. Try again");
            pin_clear();
        }
        return;
    }

    if (!s_enroll_pending) {
        memcpy(s_enroll_pin, s_pin, sizeof(s_enroll_pin));
        s_enroll_pending = true;
        lv_label_set_text(s_pin_hint, "Enter it again to confirm");
        pin_clear();
        return;
    }

    if (memcmp(s_enroll_pin, s_pin, PIN_LENGTH) != 0) {
        s_enroll_pending = false;
        lv_label_set_text(s_pin_hint, "Passcodes differed. Start again");
        pin_clear();
        return;
    }

    s_pin_record.magic = PIN_MAGIC;
    esp_fill_random(s_pin_record.salt, sizeof(s_pin_record.salt));
    pin_hash(s_pin_record.salt, s_pin, s_pin_record.digest);
    if (aura_settings_set_blob("lock_pin", &s_pin_record,
                               sizeof(s_pin_record)) == ESP_OK) {
        s_has_pin = true;
        s_enroll_pending = false;
        show_launcher();
    } else {
        lv_label_set_text(s_pin_hint, "Could not save passcode");
        pin_clear();
    }
}

static void pin_key_cb(lv_event_t *event)
{
    lv_obj_t *matrix = lv_event_get_target(event);
    uint32_t selected = lv_buttonmatrix_get_selected_button(matrix);
    const char *key = lv_buttonmatrix_get_button_text(matrix, selected);
    if (!key) return;

    if (strcmp(key, "Back") == 0) {
        if (s_pin_len) s_pin[--s_pin_len] = '\0';
        pin_render_value();
        return;
    }
    if (key[0] < '0' || key[0] > '9' || key[1] != '\0' ||
        s_pin_len >= PIN_LENGTH) return;
    s_pin[s_pin_len++] = key[0];
    s_pin[s_pin_len] = '\0';
    pin_render_value();
    if (s_pin_len == PIN_LENGTH) pin_complete();
}

static void lock_tapped(lv_event_t *event)
{
    (void)event;
    show_unlock();
}

static void launcher_lock_cb(lv_event_t *event)
{
    (void)event;
    show_lock();
}

static void build_unlock_screen(void)
{
    s_unlock_screen = lv_obj_create(s_root);
    style_screen(s_unlock_screen);

    lv_obj_t *title = lv_label_create(s_unlock_screen);
    lv_label_set_text(title, "Unlock Aura");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, AURA_SCREEN_SAFE_TOP);

    s_pin_hint = lv_label_create(s_unlock_screen);
    lv_obj_set_width(s_pin_hint, 360);
    lv_obj_set_style_text_align(s_pin_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_pin_hint, AURA_COL_MUTED, 0);
    lv_obj_align(s_pin_hint, LV_ALIGN_TOP_MID, 0, 78);

    s_pin_value = lv_label_create(s_unlock_screen);
    lv_obj_set_style_text_font(s_pin_value, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_pin_value, AURA_COL_ACCENT, 0);
    lv_obj_align(s_pin_value, LV_ALIGN_TOP_MID, 0, 108);

    static const char *keys[] = {
        "1", "2", "3", "\n",
        "4", "5", "6", "\n",
        "7", "8", "9", "\n",
        "Back", "0", "",
    };
    lv_obj_t *keypad = lv_buttonmatrix_create(s_unlock_screen);
    lv_buttonmatrix_set_map(keypad, keys);
    lv_obj_set_size(keypad, 362, 280);
    lv_obj_align(keypad, LV_ALIGN_BOTTOM_MID, 0, -AURA_SCREEN_SAFE_BOTTOM);
    lv_obj_set_style_bg_color(keypad, AURA_COL_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(keypad, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(keypad, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(keypad, 7, LV_PART_MAIN);
    lv_obj_set_style_pad_row(keypad, 7, LV_PART_MAIN);
    lv_obj_set_style_pad_column(keypad, 7, LV_PART_MAIN);
    lv_obj_set_style_bg_color(keypad, lv_color_hex(0x17171A), LV_PART_ITEMS);
    lv_obj_set_style_radius(keypad, 18, LV_PART_ITEMS);
    lv_obj_set_style_text_font(keypad, &lv_font_montserrat_28, LV_PART_ITEMS);
    lv_obj_add_event_cb(keypad, pin_key_cb, LV_EVENT_VALUE_CHANGED, NULL);
    pin_clear();
}

/* ---------------- launcher and applications ---------------- */

static void app_open_cb(lv_event_t *event)
{
    uintptr_t index = (uintptr_t)lv_event_get_user_data(event);
    if (index >= 6) return;
    s_current_app = s_apps[index];
    s_state = UI_APP;
    show_only(s_current_app);
    /* Mission, Capture/Memory, and Calendar render the durable cache first,
     * then update in place when the fresh digest arrives. Opening a connected
     * app is an explicit user request and should not wait for the background
     * ten-minute poll. */
    if (index == 0 || index == 2 || index == 3)
        aura_sync_request_refresh();
}

/* System back gesture: only touches beginning in the outer 30 px qualify.
 * This preserves vertical scrolling and in-app horizontal controls while
 * making a deliberate inward swipe from either curved edge act as Back. */
static void edge_back_cb(lv_event_t *event)
{
    lv_indev_t *indev = (lv_indev_t *)lv_event_get_current_target(event);
    lv_event_code_t code = lv_event_get_code(event);
    lv_point_t point;
    lv_indev_get_point(indev, &point);

    if (code == LV_EVENT_PRESSED) {
        s_edge_tracking = s_state != UI_LOCKED &&
                          (point.x <= 30 || point.x >= AURA_LCD_H_RES - 31);
        s_edge_from_left = point.x <= 30;
        s_edge_start = point;
        return;
    }
    if (!s_edge_tracking) return;

    if (code == LV_EVENT_PRESSING) {
        int dx = point.x - s_edge_start.x;
        int dy = point.y - s_edge_start.y;
        bool inward = s_edge_from_left ? dx >= 80 : dx <= -80;
        if (inward && LV_ABS(dy) <= 90) {
            s_edge_tracking = false;
            navigate_back();
            /* Do not let the underlying list/button receive the completed
             * gesture and accidentally scroll or click after navigation. */
            lv_indev_stop_processing(indev);
            lv_indev_wait_release(indev);
        }
    } else if (code == LV_EVENT_RELEASED) {
        s_edge_tracking = false;
    }
}

static void launcher_button(lv_obj_t *parent, int index, int x, int y,
                            const char *symbol, const char *name)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, 169, 108);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x111114), 0);
    lv_obj_set_style_radius(button, 22, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);

    lv_obj_t *icon = lv_label_create(button);
    lv_label_set_text(icon, symbol);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(icon, AURA_COL_ACCENT, 0);
    lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 5);

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, name);
    lv_obj_set_width(label, 150);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_add_event_cb(button, app_open_cb, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)index);
}

static void build_launcher(void)
{
    s_launcher = lv_obj_create(s_root);
    style_screen(s_launcher);

    lv_obj_t *title = lv_label_create(s_launcher);
    lv_label_set_text(title, "Aura");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, AURA_SCREEN_SAFE_SIDE,
                 AURA_SCREEN_SAFE_TOP);

    lv_obj_t *lock = lv_button_create(s_launcher);
    lv_obj_set_size(lock, 90, 52);
    lv_obj_set_style_bg_color(lock, lv_color_hex(0x1C1C1F), 0);
    lv_obj_set_style_radius(lock, 18, 0);
    lv_obj_align(lock, LV_ALIGN_TOP_RIGHT, -AURA_SCREEN_SAFE_SIDE,
                 AURA_SCREEN_SAFE_TOP - 8);
    lv_obj_t *label = lv_label_create(lock);
    lv_label_set_text(label, LV_SYMBOL_HOME " Lock");
    lv_obj_center(label);
    lv_obj_add_event_cb(lock, launcher_lock_cb, LV_EVENT_CLICKED, NULL);

    launcher_button(s_launcher, 0, 24, 100, LV_SYMBOL_LIST, "Mission");
    launcher_button(s_launcher, 1, 217, 100, LV_SYMBOL_BELL, "Timer");
    launcher_button(s_launcher, 2, 24, 222, LV_SYMBOL_AUDIO, "Capture");
    launcher_button(s_launcher, 3, 217, 222, LV_SYMBOL_FILE, "Calendar");
    launcher_button(s_launcher, 4, 24, 344, LV_SYMBOL_BELL, "Notifications");
    launcher_button(s_launcher, 5, 217, 344, LV_SYMBOL_SETTINGS, "Settings");
}

static void build_app_screens(void)
{
    for (size_t i = 0; i < 6; ++i) {
        s_apps[i] = lv_obj_create(s_root);
        style_screen(s_apps[i]);
    }
    tile_mission_build(s_apps[0]);
    tile_clock_build(s_apps[1]);
    tile_memory_build(s_apps[2]);
    tile_calendar_build(s_apps[3]);
    panel_notif_build(s_apps[4]);
    tile_settings_build(s_apps[5]);
}

/* ---------------- power/button events ---------------- */

static void on_btn_back(void *h, esp_event_base_t b, int32_t id, void *d)
{
    (void)h; (void)b; (void)id; (void)d;
    bsp_display_lock(0);
    navigate_back();
    bsp_display_unlock();
}

static void on_btn_home(void *h, esp_event_base_t b, int32_t id, void *d)
{
    (void)h; (void)b; (void)id; (void)d;
    bsp_display_lock(0);
    show_lock();
    bsp_display_unlock();
}

static void on_wake(void *h, esp_event_base_t b, int32_t id, void *d)
{
    (void)h; (void)b; (void)id; (void)d;
    bsp_display_lock(0);
    show_lock();
    bsp_display_unlock();
}

/* ---------------- shell construction ---------------- */

esp_err_t aura_ui_start(void)
{
    bsp_display_lock(0);

    s_root = lv_scr_act();
    style_screen(s_root);
    pin_load();

    s_lock_screen = lv_obj_create(s_root);
    style_screen(s_lock_screen);
    tile_watchface_build(s_lock_screen);
    lv_obj_add_flag(s_lock_screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_lock_screen, lock_tapped, LV_EVENT_CLICKED, NULL);

    build_unlock_screen();
    build_launcher();
    build_app_screens();
    lv_indev_t *touch = bsp_display_get_input_dev();
    if (touch) {
        lv_indev_add_event_cb(touch, edge_back_cb, LV_EVENT_PRESSED, NULL);
        lv_indev_add_event_cb(touch, edge_back_cb, LV_EVENT_PRESSING, NULL);
        lv_indev_add_event_cb(touch, edge_back_cb, LV_EVENT_RELEASED, NULL);
    }
    show_lock();
    bsp_display_unlock();

    esp_err_t err = aura_subscribe(AURA_EVT_BTN_BACK, on_btn_back, NULL);
    if (err != ESP_OK) return err;
    err = aura_subscribe(AURA_EVT_BTN_HOME, on_btn_home, NULL);
    if (err != ESP_OK) return err;
    err = aura_subscribe(AURA_EVT_WAKE, on_wake, NULL);
    if (err != ESP_OK) return err;

    ESP_LOGI(TAG, "static lock/launcher shell up (%s)",
             s_has_pin ? "PIN enrolled" : "PIN setup required");
    return ESP_OK;
}
