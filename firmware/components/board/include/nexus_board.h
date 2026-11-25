#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *sku;
    uint16_t display_width_native;
    uint16_t display_height_native;
    uint16_t display_width_landscape;
    uint16_t display_height_landscape;
    uint32_t flash_bytes;
    uint32_t psram_bytes;
    bool has_touch;
    bool has_microphone;
    bool has_speaker_output;
    bool has_micro_sd;
    bool has_native_usb;
    bool physical_revision_verified;
} nexus_board_capabilities_t;

enum {
    NEXUS_PIN_LCD_CS = 10,
    NEXUS_PIN_LCD_DC = 46,
    NEXUS_PIN_LCD_SCLK = 12,
    NEXUS_PIN_LCD_MOSI = 11,
    NEXUS_PIN_LCD_MISO = 13,
    NEXUS_PIN_LCD_BACKLIGHT = 45,
    NEXUS_PIN_TOUCH_SDA = 16,
    NEXUS_PIN_TOUCH_SCL = 15,
    NEXUS_PIN_TOUCH_RESET = 18,
    NEXUS_PIN_TOUCH_INTERRUPT = 17,
    NEXUS_PIN_USB_D_MINUS = 19,
    NEXUS_PIN_USB_D_PLUS = 20,
    NEXUS_PIN_RGB = 42,
    NEXUS_PIN_BATTERY_ADC = 9,
    NEXUS_PIN_BOOT_BUTTON = 0,
};

const nexus_board_capabilities_t *nexus_board_capabilities(void);

#ifdef __cplusplus
}
#endif
