#include "nexus_display.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nexus_board.h"

#define NEXUS_LCD_SPI_HOST SPI2_HOST
#define NEXUS_LCD_SPI_HZ (40 * 1000 * 1000)
#define NEXUS_LCD_DMA_LINES 16
#define NEXUS_LCD_DMA_PIXELS (NEXUS_DISPLAY_WIDTH * NEXUS_LCD_DMA_LINES)

typedef struct {
    uint8_t command;
    uint8_t data[16];
    uint8_t data_length;
    uint16_t delay_ms;
} lcd_init_command_t;

static const char *TAG = "nexus_display";
static spi_device_handle_t s_lcd;
static uint16_t *s_dma_buffer;
static bool s_ready;

/* LCDWiki's ES3C40P ST7796S sequence, with the required sleep-out delay and
 * landscape MADCTL (MX + MV + BGR) made explicit. */
static const lcd_init_command_t INIT_COMMANDS[] = {
    {0xF0, {0xC3}, 1, 0},
    {0xF0, {0x96}, 1, 0},
    {0x36, {0x48}, 1, 0},
    {0x3A, {0x05}, 1, 0},
    {0xB0, {0x80}, 1, 0},
    {0xB6, {0x00, 0x02}, 2, 0},
    {0xB5, {0x02, 0x03, 0x00, 0x04}, 4, 0},
    {0xB1, {0x80, 0x10}, 2, 0},
    {0xB4, {0x00}, 1, 0},
    {0xB7, {0xC6}, 1, 0},
    {0xC5, {0x1C}, 1, 0},
    {0xE4, {0x31}, 1, 0},
    {0xE8, {0x40, 0x8A, 0x00, 0x00, 0x29, 0x19, 0xA5, 0x33}, 8, 0},
    {0xC2, {0xA7}, 1, 0},
    {0xE0, {0xF0, 0x09, 0x13, 0x12, 0x12, 0x2B, 0x3C,
            0x44, 0x4B, 0x1B, 0x18, 0x17, 0x1D, 0x21}, 14, 0},
    {0xE1, {0xF0, 0x09, 0x13, 0x0C, 0x0D, 0x27, 0x3B,
            0x44, 0x4D, 0x0B, 0x17, 0x17, 0x1D, 0x21}, 14, 0},
    {0x21, {0}, 0, 0},
    {0xF0, {0x3C}, 1, 0},
    {0xF0, {0x69}, 1, 0},
    {0x13, {0}, 0, 0},
    {0x11, {0}, 0, 120},
    {0x36, {0x68}, 1, 0},
    {0x29, {0}, 0, 20},
};

static esp_err_t spi_write(const void *data, size_t length)
{
    if (!data || length == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    spi_transaction_t transaction = {
        .length = length * 8,
    };
    if (length <= sizeof(transaction.tx_data)) {
        transaction.flags = SPI_TRANS_USE_TXDATA;
        memcpy(transaction.tx_data, data, length);
    } else {
        if (length > NEXUS_LCD_DMA_PIXELS * sizeof(uint16_t)) {
            return ESP_ERR_INVALID_SIZE;
        }
        memcpy(s_dma_buffer, data, length);
        transaction.tx_buffer = s_dma_buffer;
    }
    return spi_device_polling_transmit(s_lcd, &transaction);
}

static esp_err_t lcd_command(uint8_t command)
{
    ESP_RETURN_ON_ERROR(gpio_set_level(NEXUS_PIN_LCD_DC, 0), TAG, "set command mode");
    return spi_write(&command, sizeof(command));
}

static esp_err_t lcd_data(const void *data, size_t length)
{
    ESP_RETURN_ON_ERROR(gpio_set_level(NEXUS_PIN_LCD_DC, 1), TAG, "set data mode");
    return spi_write(data, length);
}

static esp_err_t lcd_command_data(uint8_t command, const void *data, size_t length)
{
    ESP_RETURN_ON_ERROR(lcd_command(command), TAG, "send command 0x%02x", command);
    if (length > 0) {
        ESP_RETURN_ON_ERROR(lcd_data(data, length), TAG, "send data for 0x%02x", command);
    }
    return ESP_OK;
}

static esp_err_t lcd_set_window(uint16_t x_start, uint16_t y_start,
                                uint16_t x_end, uint16_t y_end)
{
    uint8_t horizontal[] = {
        (uint8_t)(x_start >> 8), (uint8_t)x_start,
        (uint8_t)(x_end >> 8), (uint8_t)x_end,
    };
    uint8_t vertical[] = {
        (uint8_t)(y_start >> 8), (uint8_t)y_start,
        (uint8_t)(y_end >> 8), (uint8_t)y_end,
    };
    ESP_RETURN_ON_ERROR(lcd_command_data(0x2A, horizontal, sizeof(horizontal)), TAG,
                        "set column window");
    ESP_RETURN_ON_ERROR(lcd_command_data(0x2B, vertical, sizeof(vertical)), TAG,
                        "set row window");
    return lcd_command(0x2C);
}

static esp_err_t lcd_fill(uint16_t color)
{
    ESP_RETURN_ON_ERROR(lcd_set_window(0, 0, NEXUS_DISPLAY_WIDTH - 1,
                                       NEXUS_DISPLAY_HEIGHT - 1), TAG, "set fill window");
    ESP_RETURN_ON_ERROR(gpio_set_level(NEXUS_PIN_LCD_DC, 1), TAG, "set pixel mode");
    uint16_t wire_color = __builtin_bswap16(color);
    for (size_t index = 0; index < NEXUS_LCD_DMA_PIXELS; ++index) {
        s_dma_buffer[index] = wire_color;
    }
    size_t remaining = NEXUS_DISPLAY_WIDTH * NEXUS_DISPLAY_HEIGHT;
    while (remaining > 0) {
        size_t chunk = remaining < NEXUS_LCD_DMA_PIXELS ? remaining : NEXUS_LCD_DMA_PIXELS;
        spi_transaction_t transaction = {
            .length = chunk * sizeof(uint16_t) * 8,
            .tx_buffer = s_dma_buffer,
        };
        ESP_RETURN_ON_ERROR(spi_device_polling_transmit(s_lcd, &transaction), TAG,
                            "fill pixels");
        remaining -= chunk;
    }
    return ESP_OK;
}

esp_err_t nexus_display_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }

    gpio_config_t output_config = {
        .pin_bit_mask = (1ULL << NEXUS_PIN_LCD_DC) | (1ULL << NEXUS_PIN_LCD_BACKLIGHT),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&output_config), TAG, "configure control pins");
    ESP_RETURN_ON_ERROR(gpio_set_level(NEXUS_PIN_LCD_BACKLIGHT, 0), TAG,
                        "disable backlight");

    s_dma_buffer = heap_caps_malloc(NEXUS_LCD_DMA_PIXELS * sizeof(uint16_t),
                                    MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    ESP_RETURN_ON_FALSE(s_dma_buffer, ESP_ERR_NO_MEM, TAG, "allocate LCD DMA buffer");

    spi_bus_config_t bus_config = {
        .mosi_io_num = NEXUS_PIN_LCD_MOSI,
        .miso_io_num = NEXUS_PIN_LCD_MISO,
        .sclk_io_num = NEXUS_PIN_LCD_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = NEXUS_LCD_DMA_PIXELS * sizeof(uint16_t),
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(NEXUS_LCD_SPI_HOST, &bus_config, SPI_DMA_CH_AUTO),
                        TAG, "initialize LCD SPI bus");

    spi_device_interface_config_t device_config = {
        .clock_speed_hz = NEXUS_LCD_SPI_HZ,
        .mode = 0,
        .spics_io_num = NEXUS_PIN_LCD_CS,
        .queue_size = 1,
    };
    ESP_RETURN_ON_ERROR(spi_bus_add_device(NEXUS_LCD_SPI_HOST, &device_config, &s_lcd),
                        TAG, "add ST7796S device");

    vTaskDelay(pdMS_TO_TICKS(120));
    for (size_t index = 0; index < sizeof(INIT_COMMANDS) / sizeof(INIT_COMMANDS[0]); ++index) {
        const lcd_init_command_t *entry = &INIT_COMMANDS[index];
        ESP_RETURN_ON_ERROR(lcd_command_data(entry->command, entry->data, entry->data_length),
                            TAG, "initialize ST7796S");
        if (entry->delay_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(entry->delay_ms));
        }
    }

    ESP_RETURN_ON_ERROR(lcd_fill(0x0000), TAG, "clear display");
    ESP_RETURN_ON_ERROR(gpio_set_level(NEXUS_PIN_LCD_BACKLIGHT, 1), TAG,
                        "enable backlight");
    s_ready = true;
    ESP_LOGI(TAG, "ST7796S initialized at %dx%d, %d MHz SPI",
             NEXUS_DISPLAY_WIDTH, NEXUS_DISPLAY_HEIGHT, NEXUS_LCD_SPI_HZ / 1000000);
    return ESP_OK;
}

bool nexus_display_is_ready(void)
{
    return s_ready;
}

esp_err_t nexus_display_blit_rgb565(uint16_t x_start, uint16_t y_start,
                                   uint16_t x_end, uint16_t y_end,
                                   const uint16_t *pixels, size_t pixel_count)
{
    ESP_RETURN_ON_FALSE(s_ready, ESP_ERR_INVALID_STATE, TAG, "display is not ready");
    ESP_RETURN_ON_FALSE(pixels, ESP_ERR_INVALID_ARG, TAG, "pixel data is required");
    ESP_RETURN_ON_FALSE(x_start <= x_end && y_start <= y_end &&
                        x_end < NEXUS_DISPLAY_WIDTH && y_end < NEXUS_DISPLAY_HEIGHT,
                        ESP_ERR_INVALID_ARG, TAG, "invalid display area");
    size_t expected = ((size_t)x_end - x_start + 1) * ((size_t)y_end - y_start + 1);
    ESP_RETURN_ON_FALSE(pixel_count == expected, ESP_ERR_INVALID_SIZE, TAG,
                        "pixel count does not match area");

    ESP_RETURN_ON_ERROR(lcd_set_window(x_start, y_start, x_end, y_end), TAG,
                        "set blit window");
    ESP_RETURN_ON_ERROR(gpio_set_level(NEXUS_PIN_LCD_DC, 1), TAG, "set pixel mode");

    size_t sent = 0;
    while (sent < pixel_count) {
        size_t remaining = pixel_count - sent;
        size_t chunk = remaining < NEXUS_LCD_DMA_PIXELS ? remaining : NEXUS_LCD_DMA_PIXELS;
        for (size_t index = 0; index < chunk; ++index) {
            s_dma_buffer[index] = __builtin_bswap16(pixels[sent + index]);
        }
        spi_transaction_t transaction = {
            .length = chunk * sizeof(uint16_t) * 8,
            .tx_buffer = s_dma_buffer,
        };
        ESP_RETURN_ON_ERROR(spi_device_polling_transmit(s_lcd, &transaction), TAG,
                            "write display pixels");
        sent += chunk;
    }
    return ESP_OK;
}
