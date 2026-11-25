#pragma once

#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NEXUS_DEVICE_MAX_FRAME_BYTES 8192U

typedef void (*nexus_transport_message_cb_t)(const char *json, size_t length, void *context);

esp_err_t nexus_transport_init(nexus_transport_message_cb_t callback, void *context);
esp_err_t nexus_transport_send_json(const char *json, size_t length);

#ifdef __cplusplus
}
#endif
