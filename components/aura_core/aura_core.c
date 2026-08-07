#include "aura_core.h"
#include "aura_config.h"
#include "aura_logic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "esp_timer.h"

static const char *TAG = "aura_core";

ESP_EVENT_DEFINE_BASE(AURA_EVENT);

static esp_event_loop_handle_t s_loop;
static nvs_handle_t s_nvs;
static aura_mode_t s_mode = AURA_MODE_SILENT; /* boot silent: user opts in to capture */
static i2c_master_bus_handle_t s_i2c;
static i2c_master_dev_handle_t s_rtc;
static bool s_clock_24h = true;
static int s_utc_offset_minutes;

/* ============================ Event bus ============================ */

esp_err_t aura_post(aura_event_id_t id, const void *data, size_t len)
{
    return esp_event_post_to(s_loop, AURA_EVENT, id, (void *)data, len, pdMS_TO_TICKS(50));
}

esp_err_t aura_subscribe(aura_event_id_t id, esp_event_handler_t h, void *arg)
{
    return esp_event_handler_register_with(s_loop, AURA_EVENT, id, h, arg);
}

aura_mode_t aura_mode_get(void) { return s_mode; }

void aura_mode_set(aura_mode_t m)
{
    if (m < AURA_MODE_SILENT || m > AURA_MODE_STEALTH) return;
    if (m == s_mode) return;
    s_mode = m;
    esp_err_t e = aura_post(AURA_EVT_MODE_CHANGED, &m, sizeof m);
    if (e != ESP_OK) ESP_LOGW(TAG, "failed to post mode change: %s", esp_err_to_name(e));
    ESP_LOGI(TAG, "mode -> %d", m);
}

/* ============================ Settings ============================ */

esp_err_t aura_settings_get_i32(const char *key, int32_t *out, int32_t dflt)
{
    if (nvs_get_i32(s_nvs, key, out) != ESP_OK) *out = dflt;
    return ESP_OK;
}

esp_err_t aura_settings_set_i32(const char *key, int32_t val)
{
    esp_err_t e = nvs_set_i32(s_nvs, key, val);
    return e == ESP_OK ? nvs_commit(s_nvs) : e;
}

esp_err_t aura_settings_get_str(const char *key, char *out, size_t len, const char *dflt)
{
    size_t l = len;
    if (nvs_get_str(s_nvs, key, out, &l) != ESP_OK) {
        strlcpy(out, dflt ? dflt : "", len);
    }
    return ESP_OK;
}

esp_err_t aura_settings_set_str(const char *key, const char *val)
{
    esp_err_t e = nvs_set_str(s_nvs, key, val);
    return e == ESP_OK ? nvs_commit(s_nvs) : e;
}

esp_err_t aura_settings_get_blob(const char *key, void *out, size_t *len)
{
    return nvs_get_blob(s_nvs, key, out, len);
}

esp_err_t aura_settings_set_blob(const char *key, const void *data, size_t len)
{
    esp_err_t e = nvs_set_blob(s_nvs, key, data, len);
    return e == ESP_OK ? nvs_commit(s_nvs) : e;
}

static void apply_timezone(void)
{
    /* POSIX TZ offsets have the opposite sign to common UTC offsets. */
    int posix_minutes = -s_utc_offset_minutes;
    char tz[32];
    snprintf(tz, sizeof(tz), "<AURA>%c%d:%02d",
             posix_minutes < 0 ? '-' : '+', abs(posix_minutes) / 60,
             abs(posix_minutes) % 60);
    setenv("TZ", tz, 1);
    tzset();
}

bool aura_time_is_24h(void) { return s_clock_24h; }
int aura_timezone_offset_minutes(void) { return s_utc_offset_minutes; }

esp_err_t aura_time_set_24h(bool enabled)
{
    s_clock_24h = enabled;
    esp_err_t err = aura_settings_set_i32("clock24", enabled ? 1 : 0);
    if (err == ESP_OK) (void)aura_post(AURA_EVT_SETTINGS_CHANGED, NULL, 0);
    return err;
}

esp_err_t aura_timezone_set_offset_minutes(int minutes)
{
    if (minutes < -12 * 60 || minutes > 14 * 60 || minutes % 30 != 0)
        return ESP_ERR_INVALID_ARG;
    s_utc_offset_minutes = minutes;
    apply_timezone();
    esp_err_t err = aura_settings_set_i32("utc_offset", minutes);
    if (err == ESP_OK) (void)aura_post(AURA_EVT_SETTINGS_CHANGED, NULL, 0);
    return err;
}

/* ====================== PCF85063 RTC <-> system time ======================
 * Registers per NXP PCF85063A datasheet:
 * 0x04 seconds (BCD, bit7 = OS/VL flag), 0x05 minutes, 0x06 hours,
 * 0x07 days, 0x08 weekdays, 0x09 months, 0x0A years (00-99 => 2000-2099).
 */
#define RTC_REG_SEC 0x04

static uint8_t bcd2dec(uint8_t b) { return (b >> 4) * 10 + (b & 0x0F); }
static uint8_t dec2bcd(uint8_t d) { return ((d / 10) << 4) | (d % 10); }

static esp_err_t rtc_read_tm(struct tm *t)
{
    uint8_t reg = RTC_REG_SEC, buf[7];
    esp_err_t e = i2c_master_transmit_receive(s_rtc, &reg, 1, buf, sizeof buf, 100);
    if (e != ESP_OK) return e;
    memset(t, 0, sizeof *t);
    t->tm_sec  = bcd2dec(buf[0] & 0x7F);
    t->tm_min  = bcd2dec(buf[1] & 0x7F);
    t->tm_hour = bcd2dec(buf[2] & 0x3F);
    t->tm_mday = bcd2dec(buf[3] & 0x3F);
    t->tm_wday = buf[4] & 0x07;
    t->tm_mon  = bcd2dec(buf[5] & 0x1F) - 1;
    t->tm_year = bcd2dec(buf[6]) + 100;      /* years since 1900 */
    t->tm_isdst = -1;
    return (buf[0] & 0x80) || !aura_time_fields_valid(t)
               ? ESP_ERR_INVALID_STATE
               : ESP_OK;
}

static esp_err_t rtc_write_tm(const struct tm *t)
{
    uint8_t buf[8] = {
        RTC_REG_SEC,
        dec2bcd(t->tm_sec), dec2bcd(t->tm_min), dec2bcd(t->tm_hour),
        dec2bcd(t->tm_mday), (uint8_t)t->tm_wday,
        dec2bcd(t->tm_mon + 1), dec2bcd(t->tm_year - 100),
    };
    return i2c_master_transmit(s_rtc, buf, sizeof buf, 100);
}

esp_err_t aura_time_init(void)
{
    struct tm t;
    if (rtc_read_tm(&t) == ESP_OK) {
        struct timeval tv = { .tv_sec = mktime(&t) };
        settimeofday(&tv, NULL);
        ESP_LOGI(TAG, "system time restored from RTC");
    } else {
        ESP_LOGW(TAG, "RTC time invalid; will set on first SNTP sync");
    }
    return ESP_OK;
}

void aura_time_now(struct tm *out)
{
    time_t now = time(NULL);
    localtime_r(&now, out);
}

esp_err_t aura_time_set(const struct tm *t)
{
    if (!aura_time_fields_valid(t)) return ESP_ERR_INVALID_ARG;
    struct tm normalized = *t;
    time_t epoch = mktime(&normalized);
    if (epoch == (time_t)-1) return ESP_ERR_INVALID_ARG;
    struct timeval tv = { .tv_sec = epoch };
    settimeofday(&tv, NULL);
    return rtc_write_tm(&normalized);
}

/* ====================== Minute tick for the watch face ====================== */

static void minute_tick_cb(void *arg)
{
    aura_post(AURA_EVT_MINUTE_TICK, NULL, 0);
}

/* ============================ Init ============================ */

esp_err_t aura_core_init(void)
{
    /* NVS */
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    ESP_ERROR_CHECK(nvs_open("aura", NVS_READWRITE, &s_nvs));

    /* Audio modes are deliberately not restored after reboot. The microphone
     * always starts hard-off and requires a fresh physical/user action. */
    int32_t m;
    s_mode = AURA_MODE_SILENT;
    (void)aura_settings_set_i32("mode", AURA_MODE_SILENT); /* migrate old builds */
    aura_settings_get_i32("clock24", &m, 1);
    s_clock_24h = m != 0;
    int32_t est_seeded;
    aura_settings_get_i32("est_seeded", &est_seeded, 0);
    if (!est_seeded) {
        /* One-time migration for the owner's fixed EST location. Keep the
         * setting editable afterward rather than overwriting it every boot. */
        (void)aura_settings_set_i32("utc_offset", -5 * 60);
        (void)aura_settings_set_i32("est_seeded", 1);
        m = -5 * 60;
    } else {
        aura_settings_get_i32("utc_offset", &m, -5 * 60);
    }
    s_utc_offset_minutes = (m >= -12 * 60 && m <= 14 * 60 && m % 30 == 0) ? m : -5 * 60;
    apply_timezone();

    /* Private event loop with its own task */
    esp_event_loop_args_t la = {
        .queue_size = 24,
        .task_name = "aura_evt",
        .task_priority = 5,
        .task_stack_size = 4096,
        .task_core_id = 0,
    };
    ESP_ERROR_CHECK(esp_event_loop_create(&la, &s_loop));

    /* Shared I2C bus (touch is owned by the BSP on the same pins; we attach
     * our sensor devices to a second master handle only if the BSP exposes
     * its bus — otherwise we create it here first and pass it to the BSP.
     * The Waveshare BSP creates the bus itself, so we fetch it: */
    /* NOTE: esp-bsp exposes bsp_i2c_get_handle(); wired up in main. */
    extern i2c_master_bus_handle_t aura_shared_i2c(void);
    s_i2c = aura_shared_i2c();

    i2c_device_config_t rtc_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AURA_ADDR_PCF85063,
        .scl_speed_hz = 100000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(s_i2c, &rtc_cfg, &s_rtc));

    aura_time_init();

    /* Fire a tick at the top of every minute-ish (1 s resolution timer that
     * posts only when the minute changes keeps the face exact). */
    static int last_min = -1;
    const esp_timer_create_args_t ta = {
        .callback = minute_tick_cb,
        .name = "time_tick",
        .skip_unhandled_events = true,
    };
    (void)last_min;
    esp_timer_handle_t th;
    ESP_ERROR_CHECK(esp_timer_create(&ta, &th));
    ESP_ERROR_CHECK(esp_timer_start_periodic(th, 1000 * 1000)); /* 1 s; UI dedupes */

    aura_post(AURA_EVT_BOOT_DONE, NULL, 0);
    ESP_LOGI(TAG, "core up (mode=%d)", s_mode);
    return ESP_OK;
}
