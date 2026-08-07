#include "aura_logic.h"

bool aura_time_fields_valid(const struct tm *t)
{
    return t &&
           t->tm_sec >= 0 && t->tm_sec <= 59 &&
           t->tm_min >= 0 && t->tm_min <= 59 &&
           t->tm_hour >= 0 && t->tm_hour <= 23 &&
           t->tm_mday >= 1 && t->tm_mday <= 31 &&
           t->tm_wday >= 0 && t->tm_wday <= 6 &&
           t->tm_mon >= 0 && t->tm_mon <= 11 &&
           t->tm_year >= 100 && t->tm_year <= 199;
}

bool aura_minute_is_new(time_t now, time_t *last_epoch_min)
{
    if (!last_epoch_min || now < 0) return false;
    time_t epoch_min = now / 60;
    if (*last_epoch_min == epoch_min) return false;
    *last_epoch_min = epoch_min;
    return true;
}
