/**
 * AURA OS — main entry
 * Boot order matters: display first (instant visual feedback), then core
 * services, then power management, then apps.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_app_desc.h"
#include "bsp/esp-bsp.h"
#include "lvgl.h"

#include "aura_core.h"
#include "aura_config.h"
#include "aura_hw.h"
#include "aura_wifi.h"
#include "aura_sync.h"
#include "aura_notifications.h"
#include "power_mgr.h"
#include "aura_ui.h"

static const char *TAG = "aura";

/* aura_core attaches its I2C devices (RTC, later IMU) to the same bus the
 * BSP created for touch/PMU/codec. */
i2c_master_bus_handle_t aura_shared_i2c(void)
{
    return bsp_i2c_get_handle();
}

/* ---------------- alarm/timer ring ---------------- */

static void ring_dismiss_cb(lv_event_t *e)
{
    lv_obj_del((lv_obj_t *)lv_event_get_user_data(e));
    power_mgr_wake_lock_release(POWER_WAKE_LOCK_ALARM);
}

static void on_alarm_fired(void *h, esp_event_base_t base, int32_t id, void *data)
{
    int which = *(int *)data;
    power_mgr_wake_lock_acquire(POWER_WAKE_LOCK_ALARM);
    power_mgr_kick();

    bsp_display_lock(0);
    lv_obj_t *m = lv_obj_create(lv_layer_top());
    lv_obj_set_size(m, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(m, lv_color_black(), 0);
    aura_ui_apply_safe_area(m);

    lv_obj_t *l = lv_label_create(m);
    lv_label_set_text(l, which < 0 ? "Timer done" : "Alarm");
    lv_obj_set_style_text_font(l, &lv_font_montserrat_48, 0);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, -60);

    lv_obj_t *btn = aura_ui_big_button(m, "Dismiss", lv_color_hex(0x0B2A3D));
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -AURA_SCREEN_SAFE_BOTTOM);
    lv_obj_add_event_cb(btn, ring_dismiss_cb, LV_EVENT_CLICKED, m);
    bsp_display_unlock();

    esp_err_t err = aura_hw_play_chime();
    if (err != ESP_OK) ESP_LOGW(TAG, "chime not queued: %s", esp_err_to_name(err));
    ESP_LOGI(TAG, "RING (%d)", which);
}

void app_main(void)
{
    ESP_LOGI(TAG, "AURA OS %s booting", esp_app_get_description()->version);

    /* 1. Display + LVGL (BSP handles CO5300 QSPI, FT3168 touch, buffers) */
    ESP_ERROR_CHECK(bsp_display_start() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(bsp_display_brightness_set(AURA_ACTIVE_BRIGHTNESS_PCT));

    /* 2. Core services: NVS settings, event bus, RTC-backed time */
    ESP_ERROR_CHECK(aura_core_init());

    /* 3. Load power preferences before the UI reads them. */
    ESP_ERROR_CHECK(power_mgr_init());
    /* Load the offline digest before connected tiles are constructed. The
     * refresh task waits for Wi-Fi and updates it in the background. */
    ESP_ERROR_CHECK(aura_sync_init());

    /* 4. UI shell + built-in apps */
    ESP_ERROR_CHECK(aura_ui_start());

    /* 5. Board services: real battery/PWR key and alarm speaker */
    ESP_ERROR_CHECK(aura_hw_init());
    ESP_ERROR_CHECK(aura_wifi_init());
    ESP_ERROR_CHECK(aura_notifications_init());
    ESP_ERROR_CHECK(aura_subscribe(AURA_EVT_ALARM_FIRED, on_alarm_fired, NULL));

    ESP_LOGI(TAG, "boot complete");
}
