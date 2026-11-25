#include "nexus_ui.h"

#include <inttypes.h>
#include <stdio.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "nexus_display.h"
#include "nexus_touch.h"

#define NEXUS_LVGL_BUFFER_LINES 40

static const char *TAG = "nexus_ui";
static bool s_ready;
static lv_color_t *s_draw_buffer;
static lv_disp_draw_buf_t s_display_buffer;
static lv_disp_drv_t s_display_driver;
static lv_indev_drv_t s_touch_driver;
static lv_obj_t *s_touch_label;
static uint32_t s_ui_taps;

static void display_flush(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *pixels)
{
    (void)driver;
    size_t count = (size_t)(area->x2 - area->x1 + 1) * (size_t)(area->y2 - area->y1 + 1);
    esp_err_t result = nexus_display_blit_rgb565((uint16_t)area->x1, (uint16_t)area->y1,
                                                 (uint16_t)area->x2, (uint16_t)area->y2,
                                                 (const uint16_t *)pixels, count);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "display flush failed: %s", esp_err_to_name(result));
    }
    lv_disp_flush_ready(driver);
}

static void touch_read(lv_indev_drv_t *driver, lv_indev_data_t *data)
{
    (void)driver;
    nexus_touch_state_t state = {0};
    nexus_touch_get_state(&state);
    data->state = state.pressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
    data->point.x = state.x;
    data->point.y = state.y;
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text, lv_color_t color,
                            lv_coord_t x, lv_coord_t y)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_pos(label, x, y);
    return label;
}

static lv_obj_t *make_card(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                           lv_coord_t width, lv_coord_t height,
                           const char *title, const char *detail, lv_color_t accent,
                           bool interactive)
{
    lv_obj_t *card = interactive ? lv_btn_create(parent) : lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, width, height);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x111A28), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x243249), 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 14, 0);

    lv_obj_t *indicator = lv_obj_create(card);
    lv_obj_set_size(indicator, 8, 8);
    lv_obj_set_pos(indicator, 0, 2);
    lv_obj_set_style_radius(indicator, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(indicator, accent, 0);
    lv_obj_set_style_bg_opa(indicator, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(indicator, 0, 0);
    lv_obj_clear_flag(indicator, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    make_label(card, title, lv_color_hex(0xF2F6FC), 16, -2);
    lv_obj_t *detail_label = make_label(card, detail, lv_color_hex(0x8190A7), 0, 28);
    lv_label_set_long_mode(detail_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(detail_label, width - 28);
    return card;
}

static void touch_card_clicked(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }
    s_ui_taps++;
    char text[64];
    snprintf(text, sizeof(text), "TOUCH VERIFIED  /  TAP %" PRIu32, s_ui_taps);
    lv_label_set_text(s_touch_label, text);
    lv_obj_set_style_text_color(s_touch_label, lv_color_hex(0x5CE1A5), 0);
}

static void build_screen(lv_indev_t *touch_input)
{
    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x070B12), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *brand = make_label(screen, "NEXUS", lv_color_hex(0xF4F7FB), 20, 14);
    lv_obj_set_style_text_letter_space(brand, 4, 0);
    make_label(screen, "PERSONAL INTELLIGENCE DOCK", lv_color_hex(0x65738A), 112, 14);
    make_label(screen, "HARDWARE ONLINE", lv_color_hex(0x5CE1A5), 352, 14);

    make_card(screen, 20, 52, 136, 188, "DISPLAY",
              "ST7796S\n480 x 320\nRGB565\n40 MHz SPI", lv_color_hex(0x5B8CFF), false);
    lv_obj_t *touch_card = make_card(screen, 172, 52, 136, 188, "TOUCH",
                                     "FT6336U\nI2C 0x38\n\nTAP THIS CARD",
                                     lv_color_hex(0x5CE1A5), true);
    lv_obj_add_event_cb(touch_card, touch_card_clicked, LV_EVENT_CLICKED, NULL);
    make_card(screen, 324, 52, 136, 188, "LINK",
              "NEXUS 1.0\nUSB SERIAL\nFRAMED JSON\nCONNECTED", lv_color_hex(0xC98BFF), false);

    s_touch_label = make_label(screen, "TOUCH READY  /  WAITING FOR INPUT",
                               lv_color_hex(0x8290A6), 20, 262);
    make_label(screen, "BUILD 0.2.2", lv_color_hex(0x46536A), 372, 262);

    lv_obj_t *cursor = lv_obj_create(screen);
    lv_obj_set_size(cursor, 18, 18);
    lv_obj_set_style_radius(cursor, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(cursor, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(cursor, LV_OPA_70, 0);
    lv_obj_set_style_border_width(cursor, 2, 0);
    lv_obj_set_style_border_color(cursor, lv_color_hex(0x5CE1A5), 0);
    lv_obj_clear_flag(cursor, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_indev_set_cursor(touch_input, cursor);
}

static void ui_task(void *context)
{
    (void)context;
    lv_init();

    size_t pixel_count = NEXUS_DISPLAY_WIDTH * NEXUS_LVGL_BUFFER_LINES;
    s_draw_buffer = heap_caps_malloc(pixel_count * sizeof(lv_color_t),
                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_draw_buffer) {
        ESP_LOGE(TAG, "failed to allocate LVGL draw buffer");
        vTaskDelete(NULL);
        return;
    }

    lv_disp_draw_buf_init(&s_display_buffer, s_draw_buffer, NULL, pixel_count);
    lv_disp_drv_init(&s_display_driver);
    s_display_driver.hor_res = NEXUS_DISPLAY_WIDTH;
    s_display_driver.ver_res = NEXUS_DISPLAY_HEIGHT;
    s_display_driver.flush_cb = display_flush;
    s_display_driver.draw_buf = &s_display_buffer;
    lv_disp_drv_register(&s_display_driver);

    lv_indev_drv_init(&s_touch_driver);
    s_touch_driver.type = LV_INDEV_TYPE_POINTER;
    s_touch_driver.read_cb = touch_read;
    lv_indev_t *touch_input = lv_indev_drv_register(&s_touch_driver);

    build_screen(touch_input);
    s_ready = true;
    ESP_LOGI(TAG, "LVGL hardware diagnostic UI started");

    for (;;) {
        lv_tick_inc(5);
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

esp_err_t nexus_ui_start(void)
{
    if (s_ready) {
        return ESP_OK;
    }
    ESP_RETURN_ON_FALSE(nexus_display_is_ready(), ESP_ERR_INVALID_STATE, TAG,
                        "display must be initialized first");
    BaseType_t created = xTaskCreate(ui_task, "nexus_ui", 8192, NULL, 4, NULL);
    return created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

bool nexus_ui_is_ready(void)
{
    return s_ready;
}
