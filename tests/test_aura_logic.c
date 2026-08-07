#include <assert.h>
#include <stdio.h>
#include "aura_logic.h"

static void test_time_validation(void)
{
    struct tm valid = {
        .tm_sec = 59, .tm_min = 59, .tm_hour = 23,
        .tm_mday = 31, .tm_wday = 6, .tm_mon = 11, .tm_year = 126,
    };
    assert(aura_time_fields_valid(&valid));
    valid.tm_mon = 12;
    assert(!aura_time_fields_valid(&valid));
    assert(!aura_time_fields_valid(NULL));
}

static void test_minute_deduplication(void)
{
    time_t last = (time_t)-1;
    assert(aura_minute_is_new(60, &last));
    assert(!aura_minute_is_new(119, &last));
    assert(aura_minute_is_new(120, &last));
    assert(aura_minute_is_new(86460, &last));
    assert(!aura_minute_is_new(86460, NULL));
}

int main(void)
{
    test_time_validation();
    test_minute_deduplication();
    puts("aura logic tests passed");
    return 0;
}
