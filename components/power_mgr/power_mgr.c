#include "power_mgr.h"
#include "aura_core.h"
#include "aura_config.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bsp/esp-bsp.h"
#include "lvgl.h"

static const char *TAG = "power";

typedef enum { PWR_ACTIVE, PWR_DIM, PWR_ASLEEP } pwr_state_t;

static volatile pwr_state_t s_state = PWR_ACTIVE;
static int64_t s_last_activity_us;
static uint32_t s_wake_locks;
static bool s_sleep_requested;
static bool s_screen_saver_active;
static TaskHandle_t s_task;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static uint8_t s_active_brightness = AURA_ACTIVE_BRIGHTNESS_PCT;
static uint8_t s_dim_brightness = AURA_DIM_BRIGHTNESS_PCT;
static uint32_t s_sleep_timeout_ms = AURA_SLEEP_TIMEOUT_MS;
static uint32_t s_dim_timeout_ms = AURA_DIM_TIMEOUT_MS;

static void mode_changed(void *handler, esp_event_base_t base, int32_t id, void *data)
{
    (void)handler; (void)base; (void)id;
    aura_mode_t mode = data ? *(aura_mode_t *)data : aura_mode_get();
    if (mode == AURA_MODE_CAPTURE || mode == AURA_MODE_STEALTH)
        power_mgr_wake_lock_acquire(POWER_WAKE_LOCK_CAPTURE);
    else
        power_mgr_wake_lock_release(POWER_WAKE_LOCK_CAPTURE);
}

void power_mgr_kick(void)
{
    s_last_activity_us = esp_timer_get_time();
    if (s_state == PWR_DIM) {
        bsp_display_brightness_set(s_active_brightness);
        s_state = PWR_ACTIVE;
    }
}

void power_mgr_stay_awake(bool on)
{
    if (on) power_mgr_wake_lock_acquire(POWER_WAKE_LOCK_USER);
    else power_mgr_wake_lock_release(POWER_WAKE_LOCK_USER);
}

void power_mgr_wake_lock_acquire(power_wake_lock_t lock)
{
    portENTER_CRITICAL(&s_lock);
    s_wake_locks |= (uint32_t)lock;
    portEXIT_CRITICAL(&s_lock);
    power_mgr_kick();
}

void power_mgr_wake_lock_release(power_wake_lock_t lock)
{
    portENTER_CRITICAL(&s_lock);
    s_wake_locks &= ~(uint32_t)lock;
    portEXIT_CRITICAL(&s_lock);
}

bool power_mgr_is_awake(void) { return s_state != PWR_ASLEEP; }

/* ---------- BOOT button: BACK (short) / HOME (long) ---------- */

static void boot_btn_task(void *arg)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << AURA_BTN_BOOT,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io);

    for (;;) {
        if (gpio_get_level(AURA_BTN_BOOT) == 0) {           /* pressed (active low) */
            int64_t t0 = esp_timer_get_time();
            while (gpio_get_level(AURA_BTN_BOOT) == 0) {
                vTaskDelay(pdMS_TO_TICKS(20));
                if (esp_timer_get_time() - t0 > 700 * 1000) break;
            }
            bool long_press = (esp_timer_get_time() - t0) > 700 * 1000;
            power_mgr_kick();
            if (s_state == PWR_ASLEEP) {
                /* button also wakes: handled by wake path, don't double-post */
            } else {
                aura_post(long_press ? AURA_EVT_BTN_HOME : AURA_EVT_BTN_BACK, NULL, 0);
            }
            /* wait for release on long press */
            while (gpio_get_level(AURA_BTN_BOOT) == 0) vTaskDelay(pdMS_TO_TICKS(20));
        }
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}

/* ---------- Sleep / wake ---------- */

static void display_off(void)
{
    aura_post(AURA_EVT_PRE_SLEEP, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(30));              /* let apps flush */
    bsp_display_lock(0);
    lv_timer_enable(false);                     /* stop LVGL work while dark */
    bsp_display_unlock();
    bsp_display_brightness_set(0);
    bsp_display_backlight_off();                /* CO5300 into sleep via BSP */
}

static void display_on(void)
{
    bsp_display_backlight_on();
    bsp_display_brightness_set(s_active_brightness);
    bsp_display_lock(0);
    lv_timer_enable(true);
    lv_obj_invalidate(lv_scr_act());
    bsp_display_unlock();
    aura_post(AURA_EVT_WAKE, NULL, 0);
}

/* Wait (short, awake) to see if the touch that woke us becomes a double tap.
 * Returns true if a second tap lands inside the window. */
static void enter_sleep_loop(void)
{
    s_state = PWR_ASLEEP;
    display_off();
    ESP_LOGI(TAG, "sleeping");

    /* Wake sources: touch INT (active low) + BOOT button (active low). */
    gpio_wakeup_enable(AURA_TP_INT, GPIO_INTR_LOW_LEVEL);
    gpio_wakeup_enable(AURA_BTN_BOOT, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();

    for (;;) {
        esp_light_sleep_start();

        uint32_t causes = esp_sleep_get_wakeup_causes();
        if (causes & BIT(ESP_SLEEP_WAKEUP_GPIO)) {
            if (gpio_get_level(AURA_BTN_BOOT) == 0) {
                break;                           /* button wake: immediate */
            }
            /* FT5x06 TP_INT is a short pulse, so waiting for a second edge
             * after light-sleep wake loses real touches unpredictably. A
             * deliberate single touch is both faster and more reliable. */
            break;
        } else {
            /* timer/other wakeups (future: alarm RTC wake) — for now, resume */
            break;
        }
    }

    gpio_wakeup_disable(AURA_TP_INT);
    gpio_wakeup_disable(AURA_BTN_BOOT);
    s_state = PWR_ACTIVE;
    s_last_activity_us = esp_timer_get_time();
    display_on();
    ESP_LOGI(TAG, "awake");
}

static void power_task(void *arg)
{
    s_last_activity_us = esp_timer_get_time();
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(500));
        portENTER_CRITICAL(&s_lock);
        uint32_t wake_locks = s_wake_locks;
        bool sleep_requested = s_sleep_requested;
        bool screen_saver_active = s_screen_saver_active;
        s_sleep_requested = false;
        portEXIT_CRITICAL(&s_lock);
        bool capture_active = (wake_locks & POWER_WAKE_LOCK_CAPTURE) != 0;
        if ((wake_locks & ~POWER_WAKE_LOCK_CAPTURE) || s_state == PWR_ASLEEP) continue;

        int64_t idle_ms = (esp_timer_get_time() - s_last_activity_us) / 1000;
        uint32_t lv_idle_ms = UINT32_MAX;
        if (bsp_display_lock(20)) {
            lv_idle_ms = lv_display_get_inactive_time(NULL);
            bsp_display_unlock();
            if ((int64_t)lv_idle_ms < idle_ms) idle_ms = lv_idle_ms;
        }

        /* LVGL owns normal touchscreen input. A fresh touch while dimmed must
         * explicitly restore ACTIVE; merely reducing idle_ms did not change
         * the state or brightness in the original FSM. */
        if (s_state == PWR_DIM && lv_idle_ms < 750) {
            power_mgr_kick();
            continue;
        }

        if (s_state == PWR_ACTIVE && idle_ms >= s_dim_timeout_ms) {
            bsp_display_brightness_set(s_dim_brightness);
            s_state = PWR_DIM;
        }
        if (!capture_active &&
            (sleep_requested ||
             (!screen_saver_active && idle_ms >= s_sleep_timeout_ms))) {
            enter_sleep_loop();
        }
    }
}

void power_mgr_sleep_now(void)
{
    portENTER_CRITICAL(&s_lock);
    s_sleep_requested = true;
    portEXIT_CRITICAL(&s_lock);
}

void power_mgr_set_screen_saver_active(bool active)
{
    portENTER_CRITICAL(&s_lock);
    s_screen_saver_active = active;
    portEXIT_CRITICAL(&s_lock);
}

uint8_t power_mgr_brightness(void) { return s_active_brightness; }
uint32_t power_mgr_sleep_timeout_seconds(void) { return s_sleep_timeout_ms / 1000; }

esp_err_t power_mgr_set_brightness(uint8_t percent)
{
    if (percent < 5 || percent > 100) return ESP_ERR_INVALID_ARG;
    s_active_brightness = percent;
    s_dim_brightness = percent < 20 ? 5 : percent / 5;
    bsp_display_brightness_set(percent);
    power_mgr_kick();
    return aura_settings_set_i32("brightness", percent);
}

esp_err_t power_mgr_set_sleep_timeout_seconds(uint32_t seconds)
{
    if (seconds < 15 || seconds > 300) return ESP_ERR_INVALID_ARG;
    s_sleep_timeout_ms = seconds * 1000;
    s_dim_timeout_ms = (seconds > 10 ? seconds - 10 : seconds) * 1000;
    power_mgr_kick();
    return aura_settings_set_i32("sleep_sec", seconds);
}

esp_err_t power_mgr_init(void)
{
    /* The BSP already configured TP_INT for its FT5x06 input driver. Calling
     * gpio_config() here would reset that interrupt configuration and make
     * LVGL touch/swipe input stop working. Reading the level and temporarily
     * enabling it as a light-sleep wake source do not require us to own or
     * reconfigure the pin. */

    int32_t saved;
    (void)aura_settings_get_i32("brightness", &saved, AURA_ACTIVE_BRIGHTNESS_PCT);
    if (saved >= 5 && saved <= 100) {
        s_active_brightness = saved;
        s_dim_brightness = saved < 20 ? 5 : saved / 5;
    }
    (void)aura_settings_get_i32("sleep_sec", &saved, AURA_SLEEP_TIMEOUT_MS / 1000);
    if (saved >= 15 && saved <= 300) {
        s_sleep_timeout_ms = saved * 1000;
        s_dim_timeout_ms = (saved > 10 ? saved - 10 : saved) * 1000;
    }
    bsp_display_brightness_set(s_active_brightness);

    (void)aura_subscribe(AURA_EVT_MODE_CHANGED, mode_changed, NULL);
    mode_changed(NULL, AURA_EVENT, AURA_EVT_MODE_CHANGED, NULL);

    if (xTaskCreatePinnedToCore(power_task, "aura_pwr", 4096, NULL, 6, &s_task, 0) != pdPASS)
        return ESP_ERR_NO_MEM;
    if (xTaskCreatePinnedToCore(boot_btn_task, "aura_btn", 3072, NULL, 5, NULL, 0) != pdPASS)
        return ESP_ERR_NO_MEM;
    ESP_LOGI(TAG, "power manager up (dim %ds, sleep %ds)",
             (int)(s_dim_timeout_ms / 1000), (int)(s_sleep_timeout_ms / 1000));
    return ESP_OK;
}
