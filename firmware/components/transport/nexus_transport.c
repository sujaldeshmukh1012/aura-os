#include "nexus_transport.h"

#include <inttypes.h>
#include <stdint.h>
#include <string.h>

#include "driver/usb_serial_jtag.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "nexus_transport";
static nexus_transport_message_cb_t s_callback;
static void *s_callback_context;
static SemaphoreHandle_t s_write_lock;

/* The default ESP-IDF USB Serial/JTAG TX ring is 256 bytes. Ring-buffer
 * writes are all-or-nothing, so keep each item comfortably below that size. */
#define NEXUS_USB_TX_CHUNK_BYTES 128

static esp_err_t write_all(const uint8_t *data, size_t length)
{
    size_t sent = 0;
    while (sent < length) {
        size_t remaining = length - sent;
        size_t chunk = remaining < NEXUS_USB_TX_CHUNK_BYTES
                           ? remaining
                           : NEXUS_USB_TX_CHUNK_BYTES;
        int result = usb_serial_jtag_write_bytes(data + sent, chunk, pdMS_TO_TICKS(250));
        if (result <= 0) {
            return ESP_ERR_TIMEOUT;
        }
        sent += (size_t)result;
    }
    return ESP_OK;
}

esp_err_t nexus_transport_send_json(const char *json, size_t length)
{
    if (!json || length == 0 || length > NEXUS_DEVICE_MAX_FRAME_BYTES) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_write_lock) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t header[4] = {
        (uint8_t)(length >> 24),
        (uint8_t)(length >> 16),
        (uint8_t)(length >> 8),
        (uint8_t)length,
    };
    if (xSemaphoreTake(s_write_lock, pdMS_TO_TICKS(500)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t result = write_all(header, sizeof(header));
    if (result == ESP_OK) {
        result = write_all((const uint8_t *)json, length);
    }
    xSemaphoreGive(s_write_lock);
    return result;
}

static void receive_task(void *context)
{
    (void)context;
    uint8_t input[128];
    uint8_t header[4];
    size_t header_length = 0;
    static char payload[NEXUS_DEVICE_MAX_FRAME_BYTES + 1];
    uint32_t expected = 0;
    size_t received = 0;

    for (;;) {
        int count = usb_serial_jtag_read_bytes(input, sizeof(input), pdMS_TO_TICKS(100));
        for (int index = 0; index < count; ++index) {
            uint8_t byte = input[index];
            if (expected == 0) {
                header[header_length++] = byte;
                if (header_length == sizeof(header)) {
                    expected = ((uint32_t)header[0] << 24) | ((uint32_t)header[1] << 16) |
                               ((uint32_t)header[2] << 8) | header[3];
                    header_length = 0;
                    received = 0;
                    if (expected == 0 || expected > NEXUS_DEVICE_MAX_FRAME_BYTES) {
                        ESP_LOGW(TAG, "rejected frame size: %" PRIu32, expected);
                        expected = 0;
                    }
                }
                continue;
            }
            payload[received++] = (char)byte;
            if (received == expected) {
                payload[received] = '\0';
                if (s_callback) {
                    s_callback(payload, received, s_callback_context);
                }
                expected = 0;
                received = 0;
            }
        }
    }
}

esp_err_t nexus_transport_init(nexus_transport_message_cb_t callback, void *context)
{
    ESP_RETURN_ON_FALSE(callback, ESP_ERR_INVALID_ARG, TAG, "callback is required");
    ESP_RETURN_ON_FALSE(!s_write_lock, ESP_ERR_INVALID_STATE, TAG, "already initialized");

    usb_serial_jtag_driver_config_t config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(usb_serial_jtag_driver_install(&config), TAG, "install USB driver");

    s_write_lock = xSemaphoreCreateMutex();
    if (!s_write_lock) {
        usb_serial_jtag_driver_uninstall();
        return ESP_ERR_NO_MEM;
    }
    s_callback = callback;
    s_callback_context = context;
    BaseType_t created = xTaskCreate(receive_task, "nexus_usb_rx", 4096, NULL, 8, NULL);
    if (created != pdPASS) {
        vSemaphoreDelete(s_write_lock);
        s_write_lock = NULL;
        usb_serial_jtag_driver_uninstall();
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
