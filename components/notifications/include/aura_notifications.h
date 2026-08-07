#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define AURA_NOTIFICATION_MAX 12

typedef struct {
    uint32_t uid;
    char app[64];
    char title[96];
    char body[192];
} aura_notification_t;

esp_err_t aura_notifications_init(void);
size_t aura_notifications_snapshot(aura_notification_t *out, size_t capacity);
size_t aura_notifications_count(void);
esp_err_t aura_notifications_dismiss(uint32_t uid);

/* Parser hooks used by the ANCS transport. */
void aura_notifications_received(uint32_t uid, const char *app,
                                 const char *title, const char *body);
void aura_notifications_removed(uint32_t uid);
void aura_ancs_init(void);
esp_err_t aura_ancs_dismiss(uint32_t uid);
