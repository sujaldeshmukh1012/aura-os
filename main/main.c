#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "aura_v2";

static void system_task(void *context)
{
    (void)context;

    for (;;) {
        ESP_LOGI(TAG, "Aura OS v2 is running");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Booting Aura OS v2 (ESP-IDF %s)", esp_get_idf_version());

    BaseType_t task_created = xTaskCreate(
        system_task,
        "aura_system",
        3072,
        NULL,
        5,
        NULL
    );

    if (task_created != pdPASS) {
        ESP_LOGE(TAG, "Failed to start the system task");
        esp_restart();
    }
}

