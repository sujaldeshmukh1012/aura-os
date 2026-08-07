#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AURA_WIFI_OFF,
    AURA_WIFI_CONNECTING,
    AURA_WIFI_CONNECTED,
    AURA_WIFI_PROVISIONING,
    AURA_WIFI_FAILED,
} aura_wifi_state_t;

esp_err_t aura_wifi_init(void);
esp_err_t aura_wifi_start_provisioning(void);
esp_err_t aura_wifi_stop_provisioning(void);
aura_wifi_state_t aura_wifi_state(void);
const char *aura_wifi_status_text(void);
const char *aura_wifi_ip(void);
const char *aura_wifi_ssid(void);
bool aura_wifi_setup_hotspot_active(void);
bool aura_wifi_time_is_synced(void);

#ifdef __cplusplus
}
#endif
