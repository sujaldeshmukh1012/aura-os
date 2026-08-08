#include "aura_notifications.h"

#include <string.h>
#include "aura_core.h"
#include "aura_hw.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static aura_notification_t s_items[AURA_NOTIFICATION_MAX];
static size_t s_count;
static SemaphoreHandle_t s_lock;
static const char *TAG = "aura_notifications";

static void changed(void)
{
    esp_err_t err = aura_post(AURA_EVT_NOTIFICATIONS_CHANGED, NULL, 0);
    if (err != ESP_OK)
        ESP_LOGW(TAG, "notification UI event dropped: %s", esp_err_to_name(err));
}

esp_err_t aura_notifications_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;
    aura_ancs_init();
    return ESP_OK;
}

size_t aura_notifications_snapshot(aura_notification_t *out, size_t capacity)
{
    if (!out || !s_lock) return 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    size_t count = s_count < capacity ? s_count : capacity;
    memcpy(out, s_items, count * sizeof(*out));
    xSemaphoreGive(s_lock);
    return count;
}

size_t aura_notifications_count(void)
{
    if (!s_lock) return 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    size_t count = s_count;
    xSemaphoreGive(s_lock);
    return count;
}

bool aura_notifications_phone_connected(void)
{
    return aura_ancs_is_connected();
}

void aura_notifications_connection_changed(void)
{
    changed();
}

void aura_notifications_received(uint32_t uid, const char *app,
                                 const char *title, const char *body)
{
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    size_t index = s_count;
    bool is_new = true;
    for (size_t i = 0; i < s_count; ++i)
        if (s_items[i].uid == uid) { index = i; is_new = false; break; }
    if (index == s_count) {
        if (s_count < AURA_NOTIFICATION_MAX) ++s_count;
        else index = AURA_NOTIFICATION_MAX - 1;
    }
    if (index > 0) memmove(&s_items[1], &s_items[0], index * sizeof(s_items[0]));
    s_items[0].uid = uid;
    strlcpy(s_items[0].app, app && app[0] ? app : "iPhone", sizeof(s_items[0].app));
    strlcpy(s_items[0].title, title && title[0] ? title : "Notification", sizeof(s_items[0].title));
    strlcpy(s_items[0].body, body ? body : "", sizeof(s_items[0].body));
    xSemaphoreGive(s_lock);
    int32_t sound_enabled = 1;
    (void)aura_settings_get_i32("notif_sound", &sound_enabled, 1);
    if (is_new && sound_enabled && aura_mode_get() != AURA_MODE_CONVERSATION)
        (void)aura_hw_play_notification();
    changed();
}

void aura_notifications_removed(uint32_t uid)
{
    if (!s_lock) return;
    bool removed = false;
    size_t remaining;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (size_t i = 0; i < s_count; ++i) {
        if (s_items[i].uid != uid) continue;
        if (i + 1 < s_count)
            memmove(&s_items[i], &s_items[i + 1], (s_count - i - 1) * sizeof(s_items[0]));
        --s_count;
        removed = true;
        break;
    }
    remaining = s_count;
    xSemaphoreGive(s_lock);
    if (removed) {
        ESP_LOGI(TAG, "removed uid=%lu; remaining=%u",
                 (unsigned long)uid, (unsigned)remaining);
        changed();
    }
}

esp_err_t aura_notifications_dismiss(uint32_t uid)
{
    esp_err_t err = aura_ancs_dismiss(uid);
    if (err == ESP_OK) aura_notifications_removed(uid);
    return err;
}
