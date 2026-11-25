#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t nexus_ui_start(void);
bool nexus_ui_is_ready(void);

#ifdef __cplusplus
}
#endif
