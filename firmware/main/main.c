#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nexus_board.h"
#include "nexus_display.h"
#include "nexus_touch.h"
#include "nexus_transport.h"
#include "nexus_ui.h"

static const char *TAG = "nexus";
static uint32_t s_sequence;
static char s_session_json[131] = "null";

static void uuid_v4(char output[37])
{
    uint8_t value[16];
    for (size_t offset = 0; offset < sizeof(value); offset += sizeof(uint32_t)) {
        uint32_t random = esp_random();
        memcpy(&value[offset], &random, sizeof(random));
    }
    value[6] = (value[6] & 0x0f) | 0x40;
    value[8] = (value[8] & 0x3f) | 0x80;
    snprintf(output, 37,
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             value[0], value[1], value[2], value[3], value[4], value[5], value[6], value[7],
             value[8], value[9], value[10], value[11], value[12], value[13], value[14], value[15]);
}

static esp_err_t send_message(const char *message_type, const char *payload,
                              const char *correlation_id)
{
    char message_id[37];
    const size_t frame_capacity = 2048;
    char *frame = malloc(frame_capacity);
    if (!frame) {
        return ESP_ERR_NO_MEM;
    }
    uuid_v4(message_id);
    s_sequence++;
    int length = snprintf(
        frame, frame_capacity,
        "{\"auth\":{\"mode\":\"development\",\"sequence\":%" PRIu32
        ",\"session_id\":%s},\"correlation_id\":%s,\"destination\":{\"id\":\"local\","
        "\"kind\":\"host\"},\"message_id\":\"%s\",\"message_type\":\"%s\","
        "\"payload\":%s,\"protocol_version\":\"1.0\",\"source\":{\"id\":\"es3c40p-dev\","
        "\"kind\":\"device\"},\"timestamp\":\"1970-01-01T00:00:00Z\"}",
        s_sequence,
        s_session_json,
        correlation_id ? correlation_id : "null",
        message_id, message_type, payload);
    if (length < 0 || (size_t)length >= frame_capacity) {
        free(frame);
        return ESP_ERR_INVALID_SIZE;
    }
    esp_err_t result = nexus_transport_send_json(frame, (size_t)length);
    free(frame);
    return result;
}

static void on_host_message(const char *json, size_t length, void *context)
{
    (void)length;
    (void)context;
    if (strstr(json, "\"message_type\":\"session.welcome\"") != NULL) {
        const char *session = strstr(json, "\"session_id\":\"");
        if (session) {
            session += strlen("\"session_id\":\"");
            const char *end = strchr(session, '\"');
            size_t session_length = end ? (size_t)(end - session) : 0;
            if (session_length > 0 && session_length <= sizeof(s_session_json) - 3) {
                s_session_json[0] = '\"';
                memcpy(&s_session_json[1], session, session_length);
                s_session_json[session_length + 1] = '\"';
                s_session_json[session_length + 2] = '\0';
            }
        }
        ESP_LOGI(TAG, "Host session established");
    } else if (strstr(json, "\"message_type\":\"heartbeat.ping\"") != NULL) {
        const char *message = strstr(json, "\"message_id\":\"");
        char correlation_json[39];
        const char *correlation = NULL;
        if (message) {
            message += strlen("\"message_id\":\"");
            const char *end = strchr(message, '\"');
            if (end && (size_t)(end - message) == 36) {
                correlation_json[0] = '\"';
                memcpy(&correlation_json[1], message, 36);
                correlation_json[37] = '\"';
                correlation_json[38] = '\0';
                correlation = correlation_json;
            }
        }
        (void)send_message("heartbeat.pong", "{\"alive\":true}", correlation);
    }
}

static void status_task(void *context)
{
    (void)context;
    char payload[512];
    for (;;) {
        nexus_touch_state_t touch = {0};
        nexus_touch_get_state(&touch);
        int64_t uptime_ms = esp_timer_get_time() / 1000;
        snprintf(payload, sizeof(payload),
                 "{\"board\":\"%s\",\"clock_synchronized\":false,"
                 "\"display_ready\":%s,\"free_heap_bytes\":%" PRIu32 ","
                 "\"touch_chip_id\":%u,\"touch_events\":%" PRIu32 ","
                 "\"touch_pressed\":%s,\"touch_ready\":%s,"
                 "\"touch_x\":%u,\"touch_y\":%u,\"ui_ready\":%s,"
                 "\"uptime_ms\":%" PRId64 "}",
                 nexus_board_capabilities()->sku,
                 nexus_display_is_ready() ? "true" : "false",
                 esp_get_free_heap_size(), touch.chip_id, touch.touch_events,
                 touch.pressed ? "true" : "false", touch.ready ? "true" : "false",
                 touch.x, touch.y, nexus_ui_is_ready() ? "true" : "false", uptime_ms);
        esp_err_t err = send_message("device.status", payload, NULL);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "status send failed: %s", esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void app_main(void)
{
    const nexus_board_capabilities_t *board = nexus_board_capabilities();
    ESP_LOGI(TAG, "NEXUS %s starting on %s", "0.2.0", board->sku);

    ESP_ERROR_CHECK(nexus_transport_init(on_host_message, NULL));

    esp_err_t display_error = nexus_display_init();
    if (display_error != ESP_OK) {
        ESP_LOGE(TAG, "display initialization failed: %s", esp_err_to_name(display_error));
    }
    esp_err_t touch_error = nexus_touch_init();
    if (touch_error != ESP_OK) {
        ESP_LOGE(TAG, "touch initialization failed: %s", esp_err_to_name(touch_error));
    }
    esp_err_t ui_error = nexus_ui_start();
    if (ui_error != ESP_OK) {
        ESP_LOGE(TAG, "UI startup failed: %s", esp_err_to_name(ui_error));
    }

    esp_err_t hello_err = send_message(
        "session.hello",
        "{\"capabilities\":[\"display.480x320\",\"touch.ft6336u\","
        "\"transport.usb_serial_jtag\",\"ui.lvgl.8.4\"],"
        "\"supported_versions\":[\"1.0\"]}",
        NULL);
    if (hello_err != ESP_OK) {
        ESP_LOGW(TAG, "initial host hello deferred: %s", esp_err_to_name(hello_err));
    }

    BaseType_t created = xTaskCreate(status_task, "nexus_status", 4096, NULL, 5, NULL);
    if (created != pdPASS) {
        ESP_LOGE(TAG, "failed to create status task");
    }
}
