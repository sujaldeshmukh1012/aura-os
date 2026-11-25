#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    NEXUS_DISPLAY_WIDTH = 480,
    NEXUS_DISPLAY_HEIGHT = 320,
};

esp_err_t nexus_display_init(void);
bool nexus_display_is_ready(void);

/* Coordinates are inclusive. Pixels are native-endian RGB565 and are converted
 * to the ST7796S wire byte order by the driver. */
esp_err_t nexus_display_blit_rgb565(uint16_t x_start, uint16_t y_start,
                                   uint16_t x_end, uint16_t y_end,
                                   const uint16_t *pixels, size_t pixel_count);

#ifdef __cplusplus
}
#endif
