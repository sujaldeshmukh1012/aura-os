#pragma once

#include <stdbool.h>
#include <time.h>

/* Pure helpers kept free of ESP-IDF dependencies so critical clock behavior
 * can be exercised on the development host. */
bool aura_time_fields_valid(const struct tm *t);
bool aura_minute_is_new(time_t now, time_t *last_epoch_min);
