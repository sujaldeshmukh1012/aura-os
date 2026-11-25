#include "nexus_touch.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nexus_board.h"

#define FT6336U_ADDRESS 0x38
#define FT6336U_REGISTER_TOUCH_DATA 0x02
#define FT6336U_REGISTER_CHIP_ID 0xA3
#define FT6336U_REGISTER_FIRMWARE_ID 0xA6
#define FT6336U_REGISTER_VENDOR_ID 0xA8
#define FT6336U_I2C_HZ 400000

static const char *TAG = "nexus_touch";
static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_device;
static nexus_touch_state_t s_state;
static portMUX_TYPE s_state_lock = portMUX_INITIALIZER_UNLOCKED;

static esp_err_t read_registers(uint8_t start, uint8_t *data, size_t length)
{
    return i2c_master_transmit_receive(s_device, &start, sizeof(start), data, length, 100);
}

static uint16_t clamp_u16(uint16_t value, uint16_t maximum)
{
    return value > maximum ? maximum : value;
}

static void touch_task(void *context)
{
    (void)context;
    bool was_pressed = false;
    for (;;) {
        uint8_t data[5] = {0};
        esp_err_t result = read_registers(FT6336U_REGISTER_TOUCH_DATA, data, sizeof(data));
        bool pressed = result == ESP_OK && (data[0] & 0x0F) > 0;
        uint16_t raw_x = 0;
        uint16_t raw_y = 0;
        uint16_t x = 0;
        uint16_t y = 0;
        uint8_t point_count = 0;

        if (pressed) {
            point_count = data[0] & 0x0F;
            raw_x = (uint16_t)(((data[1] & 0x0F) << 8) | data[2]);
            raw_y = (uint16_t)(((data[3] & 0x0F) << 8) | data[4]);

            /* The digitizer is portrait-native. MADCTL 0x28 is the standard
             * landscape rotation, so swap axes and mirror logical Y. */
            x = clamp_u16(raw_y, 479);
            y = (uint16_t)(319 - clamp_u16(raw_x, 319));
        }

        taskENTER_CRITICAL(&s_state_lock);
        s_state.pressed = pressed;
        s_state.point_count = point_count;
        if (pressed) {
            s_state.raw_x = raw_x;
            s_state.raw_y = raw_y;
            s_state.x = x;
            s_state.y = y;
            if (!was_pressed) {
                s_state.touch_events++;
            }
        }
        taskEXIT_CRITICAL(&s_state_lock);
        was_pressed = pressed;
        vTaskDelay(pdMS_TO_TICKS(16));
    }
}

esp_err_t nexus_touch_init(void)
{
    if (nexus_touch_is_ready()) {
        return ESP_OK;
    }

    gpio_config_t reset_config = {
        .pin_bit_mask = 1ULL << NEXUS_PIN_TOUCH_RESET,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&reset_config), TAG, "configure touch reset");
    gpio_config_t interrupt_config = {
        .pin_bit_mask = 1ULL << NEXUS_PIN_TOUCH_INTERRUPT,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&interrupt_config), TAG, "configure touch interrupt");

    ESP_RETURN_ON_ERROR(gpio_set_level(NEXUS_PIN_TOUCH_RESET, 0), TAG, "assert touch reset");
    vTaskDelay(pdMS_TO_TICKS(12));
    ESP_RETURN_ON_ERROR(gpio_set_level(NEXUS_PIN_TOUCH_RESET, 1), TAG, "release touch reset");
    vTaskDelay(pdMS_TO_TICKS(300));

    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = NEXUS_PIN_TOUCH_SDA,
        .scl_io_num = NEXUS_PIN_TOUCH_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_config, &s_bus), TAG,
                        "initialize shared I2C bus");
    ESP_RETURN_ON_ERROR(i2c_master_probe(s_bus, FT6336U_ADDRESS, 100), TAG,
                        "probe FT6336U at 0x38");

    i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = FT6336U_ADDRESS,
        .scl_speed_hz = FT6336U_I2C_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_bus, &device_config, &s_device), TAG,
                        "add FT6336U device");

    uint8_t chip_id = 0;
    uint8_t vendor_id = 0;
    uint8_t firmware_id = 0;
    ESP_RETURN_ON_ERROR(read_registers(FT6336U_REGISTER_CHIP_ID, &chip_id, 1), TAG,
                        "read touch chip ID");
    ESP_RETURN_ON_ERROR(read_registers(FT6336U_REGISTER_VENDOR_ID, &vendor_id, 1), TAG,
                        "read touch vendor ID");
    ESP_RETURN_ON_ERROR(read_registers(FT6336U_REGISTER_FIRMWARE_ID, &firmware_id, 1), TAG,
                        "read touch firmware ID");

    taskENTER_CRITICAL(&s_state_lock);
    s_state.ready = true;
    s_state.chip_id = chip_id;
    s_state.vendor_id = vendor_id;
    s_state.firmware_version = firmware_id;
    taskEXIT_CRITICAL(&s_state_lock);

    BaseType_t created = xTaskCreate(touch_task, "nexus_touch", 3072, NULL, 6, NULL);
    if (created != pdPASS) {
        taskENTER_CRITICAL(&s_state_lock);
        s_state.ready = false;
        taskEXIT_CRITICAL(&s_state_lock);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "FT6336U ready: chip=0x%02x vendor=0x%02x firmware=0x%02x",
             chip_id, vendor_id, firmware_id);
    return ESP_OK;
}

bool nexus_touch_is_ready(void)
{
    bool ready;
    taskENTER_CRITICAL(&s_state_lock);
    ready = s_state.ready;
    taskEXIT_CRITICAL(&s_state_lock);
    return ready;
}

void nexus_touch_get_state(nexus_touch_state_t *state)
{
    if (!state) {
        return;
    }
    taskENTER_CRITICAL(&s_state_lock);
    memcpy(state, &s_state, sizeof(*state));
    taskEXIT_CRITICAL(&s_state_lock);
}
