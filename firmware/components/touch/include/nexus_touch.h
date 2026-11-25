#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool ready;
    bool pressed;
    uint8_t point_count;
    uint8_t chip_id;
    uint8_t vendor_id;
    uint8_t firmware_version;
    uint16_t raw_x;
    uint16_t raw_y;
    uint16_t x;
    uint16_t y;
    uint32_t touch_events;
} nexus_touch_state_t;

esp_err_t nexus_touch_init(void);
bool nexus_touch_is_ready(void);
void nexus_touch_get_state(nexus_touch_state_t *state);

#ifdef __cplusplus
}
#endif
