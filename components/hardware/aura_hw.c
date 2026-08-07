#include "aura_hw.h"

#include <stdbool.h>
#include <stdint.h>
#include <math.h>

#include "aura_core.h"
#include "bsp/esp-bsp.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#undef M_PI /* Waveshare's driver declares its own float constant. */
#include "qmi8658.h"

static const char *TAG = "aura_hw";

#define AXP2101_ADDR              0x34
#define AXP2101_CHIP_ID           0x03
#define AXP2101_EXPECTED_ID       0x4A
#define AXP2101_STATUS1           0x00
#define AXP2101_STATUS2           0x01
#define AXP2101_ADC_ENABLE        0x30
#define AXP2101_BATT_VOLT_H       0x34
#define AXP2101_BATT_VOLT_L       0x35
#define AXP2101_IRQ_ENABLE2       0x41
#define AXP2101_IRQ_STATUS1       0x48
#define AXP2101_IRQ_STATUS2       0x49
#define AXP2101_IRQ_STATUS3       0x4A
#define AXP2101_BATT_PERCENT      0xA4
#define AXP2101_IRQ_PKEY_LONG     (1U << 2)
#define AXP2101_IRQ_PKEY_SHORT    (1U << 3)

#define PMU_POLL_MS               50
#define PWR_DOUBLE_CLICK_MS       600
#define BATTERY_PUBLISH_MS        30000
#define CHIME_SAMPLE_RATE         22050
#define CHIME_VOLUME              55
#define CHIME_CHUNK_SAMPLES       256

typedef enum {
    AUDIO_CUE_ALARM = 1,
    AUDIO_CUE_NOTIFICATION,
} audio_cue_t;

static i2c_master_dev_handle_t s_pmu;
static QueueHandle_t s_chime_queue;
static SemaphoreHandle_t s_audio_lock;
static qmi8658_dev_t s_imu;
static volatile uint32_t s_steps;
static int64_t s_pending_power_click_us;

esp_err_t aura_capture_start(void);

bool aura_hw_audio_acquire(uint32_t timeout_ms)
{
    return s_audio_lock && xSemaphoreTake(s_audio_lock, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void aura_hw_audio_release(void)
{
    if (s_audio_lock) xSemaphoreGive(s_audio_lock);
}

static int day_key(void)
{
    struct tm now;
    aura_time_now(&now);
    return (now.tm_year + 1900) * 1000 + now.tm_yday;
}

static void publish_steps(void)
{
    uint32_t value = s_steps;
    (void)aura_post(AURA_EVT_STEPS, &value, sizeof(value));
}

static void steps_task(void *arg)
{
    (void)arg;
    int32_t saved_day = 0, saved_steps = 0;
    (void)aura_settings_get_i32("steps_day", &saved_day, 0);
    (void)aura_settings_get_i32("steps", &saved_steps, 0);
    int current_day = day_key();
    s_steps = saved_day == current_day && saved_steps > 0 ? (uint32_t)saved_steps : 0;
    publish_steps();

    float gravity = 1000.0f;
    bool valley_seen = false;
    int64_t last_step_us = 0;
    uint32_t last_saved = s_steps;
    for (;;) {
        float x, y, z;
        if (qmi8658_read_accel(&s_imu, &x, &y, &z) == ESP_OK) {
            float magnitude = sqrtf(x * x + y * y + z * z);
            gravity += 0.06f * (magnitude - gravity);
            float motion = magnitude - gravity;
            if (motion < -55.0f) valley_seen = true;
            int64_t now_us = esp_timer_get_time();
            if (valley_seen && motion > 115.0f && now_us - last_step_us > 280000) {
                ++s_steps;
                last_step_us = now_us;
                valley_seen = false;
                publish_steps();
            }
        }

        int new_day = day_key();
        if (new_day != current_day) {
            current_day = new_day;
            s_steps = 0;
            last_saved = 0;
            publish_steps();
            (void)aura_settings_set_i32("steps_day", current_day);
            (void)aura_settings_set_i32("steps", 0);
        } else if (s_steps - last_saved >= 25) {
            last_saved = s_steps;
            (void)aura_settings_set_i32("steps_day", current_day);
            (void)aura_settings_set_i32("steps", s_steps);
        }
        vTaskDelay(pdMS_TO_TICKS(32));
    }
}

static esp_err_t reg_read(uint8_t reg, uint8_t *value)
{
    return i2c_master_transmit_receive(s_pmu, &reg, 1, value, 1, 100);
}

static esp_err_t reg_write(uint8_t reg, uint8_t value)
{
    const uint8_t bytes[] = {reg, value};
    return i2c_master_transmit(s_pmu, bytes, sizeof(bytes), 100);
}

static esp_err_t publish_battery(void)
{
    uint8_t status1, status2, percent, vh, vl;
    esp_err_t err = reg_read(AXP2101_STATUS1, &status1);
    if (err != ESP_OK) return err;
    if ((err = reg_read(AXP2101_STATUS2, &status2)) != ESP_OK) return err;
    if ((err = reg_read(AXP2101_BATT_PERCENT, &percent)) != ESP_OK) return err;
    if ((err = reg_read(AXP2101_BATT_VOLT_H, &vh)) != ESP_OK) return err;
    if ((err = reg_read(AXP2101_BATT_VOLT_L, &vl)) != ESP_OK) return err;

    aura_battery_t battery = {
        .percent = (status1 & (1U << 3)) && percent <= 100 ? percent : 0,
        .charging = ((status2 >> 5) & 0x03) == 1,
        .millivolts = (uint16_t)(((vh & 0x1F) << 8) | vl),
    };
    return aura_post(AURA_EVT_BATTERY, &battery, sizeof(battery));
}

static void post_mode_button(void)
{
    (void)aura_post(AURA_EVT_BTN_MODE_CYCLE, NULL, 0);
}

static void power_single_click(void)
{
    aura_mode_t current = aura_mode_get();
    aura_mode_t next;
    if (current == AURA_MODE_CAPTURE)
        next = AURA_MODE_STEALTH;
    else if (current == AURA_MODE_STEALTH)
        next = AURA_MODE_SILENT;
    else
        next = AURA_MODE_STEALTH;
    aura_mode_set(next);
    post_mode_button();
}

static void power_double_click(void)
{
    aura_mode_t current = aura_mode_get();
    aura_mode_set(current == AURA_MODE_CAPTURE ? AURA_MODE_STEALTH
                                                : AURA_MODE_CAPTURE);
    post_mode_button();
}

static void handle_power_key(uint8_t irq)
{
    if (irq & AXP2101_IRQ_PKEY_LONG) {
        s_pending_power_click_us = 0;
        (void)aura_post(AURA_EVT_BTN_ASSISTANT, NULL, 0);
    } else if (irq & AXP2101_IRQ_PKEY_SHORT) {
        int64_t now = esp_timer_get_time();
        if (s_pending_power_click_us != 0 &&
            now - s_pending_power_click_us <= PWR_DOUBLE_CLICK_MS * 1000LL) {
            s_pending_power_click_us = 0;
            power_double_click();
        } else {
            s_pending_power_click_us = now;
        }
    }
}

static void pmu_task(void *arg)
{
    (void)arg;
    TickType_t last_battery = 0;

    for (;;) {
        uint8_t irq = 0;
        if (reg_read(AXP2101_IRQ_STATUS2, &irq) == ESP_OK && irq != 0) {
            handle_power_key(irq);
            (void)reg_write(AXP2101_IRQ_STATUS2, irq);
        }

        if (s_pending_power_click_us != 0 &&
            esp_timer_get_time() - s_pending_power_click_us >
                PWR_DOUBLE_CLICK_MS * 1000LL) {
            s_pending_power_click_us = 0;
            power_single_click();
        }

        TickType_t now = xTaskGetTickCount();
        if (last_battery == 0 || (now - last_battery) >= pdMS_TO_TICKS(BATTERY_PUBLISH_MS)) {
            esp_err_t err = publish_battery();
            if (err != ESP_OK) ESP_LOGW(TAG, "battery read failed: %s", esp_err_to_name(err));
            last_battery = now;
        }
        vTaskDelay(pdMS_TO_TICKS(PMU_POLL_MS));
    }
}

static int codec_write_tone(esp_codec_dev_handle_t codec, uint32_t hz, uint32_t duration_ms)
{
    int16_t samples[CHIME_CHUNK_SAMPLES];
    const uint32_t total = CHIME_SAMPLE_RATE * duration_ms / 1000;
    uint32_t phase = 0;
    const uint32_t step = (uint32_t)(((uint64_t)hz << 32) / CHIME_SAMPLE_RATE);

    for (uint32_t sent = 0; sent < total;) {
        uint32_t count = total - sent;
        if (count > CHIME_CHUNK_SAMPLES) count = CHIME_CHUNK_SAMPLES;
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t pos = sent + i;
            uint32_t envelope = 4096;
            if (pos < 220) envelope = pos * 4096 / 220;
            if (total - pos < 440) envelope = (total - pos) * 4096 / 440;
            samples[i] = (phase & 0x80000000U) ? (int16_t)envelope : -(int16_t)envelope;
            phase += step;
        }
        int rc = esp_codec_dev_write(codec, samples, count * sizeof(samples[0]));
        if (rc != ESP_CODEC_DEV_OK) return rc;
        sent += count;
    }
    return ESP_CODEC_DEV_OK;
}

static void chime_task(void *arg)
{
    (void)arg;
    esp_codec_dev_handle_t codec = NULL;
    audio_cue_t command;
    for (;;) {
        if (xQueueReceive(s_chime_queue, &command, portMAX_DELAY) != pdTRUE) continue;
        if (codec == NULL) codec = bsp_audio_codec_speaker_init();
        if (codec == NULL) {
            ESP_LOGE(TAG, "speaker initialization failed");
            continue;
        }
        esp_codec_dev_sample_info_t format = {
            .bits_per_sample = 16,
            .channel = 1,
            .channel_mask = 0,
            .sample_rate = CHIME_SAMPLE_RATE,
            .mclk_multiple = 0,
        };
        if (!aura_hw_audio_acquire(1000)) continue;
        if (esp_codec_dev_set_out_vol(codec, CHIME_VOLUME) != ESP_CODEC_DEV_OK ||
            esp_codec_dev_open(codec, &format) != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "speaker open failed");
            aura_hw_audio_release();
            continue;
        }
        if (command == AUDIO_CUE_NOTIFICATION) {
            (void)codec_write_tone(codec, 1047, 95);
        } else {
            (void)codec_write_tone(codec, 880, 180);
            vTaskDelay(pdMS_TO_TICKS(70));
            (void)codec_write_tone(codec, 1175, 260);
        }
        (void)esp_codec_dev_close(codec);
        aura_hw_audio_release();
    }
}

esp_err_t aura_hw_init(void)
{
    if (s_pmu != NULL) return ESP_ERR_INVALID_STATE;

    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AXP2101_ADDR,
        .scl_speed_hz = 400000,
    };
    esp_err_t err = i2c_master_bus_add_device(bsp_i2c_get_handle(), &cfg, &s_pmu);
    if (err != ESP_OK) return err;

    uint8_t id = 0;
    if ((err = reg_read(AXP2101_CHIP_ID, &id)) != ESP_OK || id != AXP2101_EXPECTED_ID) {
        ESP_LOGE(TAG, "AXP2101 not detected (id=0x%02x, err=%s)", id, esp_err_to_name(err));
        return err == ESP_OK ? ESP_ERR_NOT_FOUND : err;
    }

    uint8_t adc = 0;
    ESP_RETURN_ON_ERROR(reg_read(AXP2101_ADC_ENABLE, &adc), TAG, "read ADC config");
    ESP_RETURN_ON_ERROR(reg_write(AXP2101_ADC_ENABLE, adc | 0x01), TAG, "enable battery ADC");
    ESP_RETURN_ON_ERROR(reg_write(AXP2101_IRQ_STATUS1, 0xFF), TAG, "clear IRQ1");
    ESP_RETURN_ON_ERROR(reg_write(AXP2101_IRQ_STATUS2, 0xFF), TAG, "clear IRQ2");
    ESP_RETURN_ON_ERROR(reg_write(AXP2101_IRQ_STATUS3, 0xFF), TAG, "clear IRQ3");
    uint8_t irq_enable = 0;
    ESP_RETURN_ON_ERROR(reg_read(AXP2101_IRQ_ENABLE2, &irq_enable), TAG, "read IRQ config");
    ESP_RETURN_ON_ERROR(reg_write(AXP2101_IRQ_ENABLE2, irq_enable |
                        AXP2101_IRQ_PKEY_SHORT | AXP2101_IRQ_PKEY_LONG), TAG, "enable PWR events");

    s_chime_queue = xQueueCreate(2, sizeof(audio_cue_t));
    s_audio_lock = xSemaphoreCreateMutex();
    if (s_chime_queue == NULL || s_audio_lock == NULL) return ESP_ERR_NO_MEM;
    if (xTaskCreate(chime_task, "aura_chime", 4096, NULL, 4, NULL) != pdPASS ||
        xTaskCreate(pmu_task, "aura_pmu", 3072, NULL, 3, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    err = aura_capture_start();
    if (err != ESP_OK) ESP_LOGW(TAG, "capture service unavailable: %s", esp_err_to_name(err));
    err = qmi8658_init(&s_imu, bsp_i2c_get_handle(), QMI8658_ADDRESS_HIGH);
    if (err == ESP_OK) {
        (void)qmi8658_set_accel_range(&s_imu, QMI8658_ACCEL_RANGE_4G);
        (void)qmi8658_set_accel_odr(&s_imu, QMI8658_ACCEL_ODR_31_25HZ);
        (void)qmi8658_enable_sensors(&s_imu, QMI8658_ENABLE_ACCEL);
        qmi8658_set_accel_unit_mg(&s_imu, true);
        if (xTaskCreate(steps_task, "aura_steps", 4096, NULL, 3, NULL) != pdPASS)
            return ESP_ERR_NO_MEM;
        ESP_LOGI(TAG, "QMI8658 pedometer active at 31.25 Hz");
    } else {
        ESP_LOGW(TAG, "QMI8658 unavailable; steps disabled: %s", esp_err_to_name(err));
    }
    ESP_LOGI(TAG, "AXP2101 ready; battery gauge and PWR key active");
    return ESP_OK;
}

uint32_t aura_hw_steps(void) { return s_steps; }

esp_err_t aura_hw_play_chime(void)
{
    if (s_chime_queue == NULL) return ESP_ERR_INVALID_STATE;
    const audio_cue_t command = AUDIO_CUE_ALARM;
    return xQueueSend(s_chime_queue, &command, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t aura_hw_play_notification(void)
{
    if (s_chime_queue == NULL) return ESP_ERR_INVALID_STATE;
    const audio_cue_t command = AUDIO_CUE_NOTIFICATION;
    return xQueueSend(s_chime_queue, &command, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}
