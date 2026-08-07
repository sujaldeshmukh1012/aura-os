#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AURA_SYNC_MAX_EVENTS 8
#define AURA_SYNC_MAX_TASKS  10
#define AURA_SYNC_MAX_PROPOSALS 6

typedef struct {
    int id;
    time_t start_epoch;
    bool all_day;
    char title[64];
    char calendar[32];
    char location[48];
} aura_sync_event_t;

typedef struct {
    int id;
    int priority;
    char title[64];
    char project[32];
    char due_at[32];
} aura_sync_task_t;

typedef struct {
    int id;
    int priority;
    char kind[12];
    char title[64];
    char details[96];
    char due_at[32];
    char starts_at[32];
} aura_sync_proposal_t;

typedef struct {
    bool has_cache;
    bool online;
    time_t updated_epoch;
    uint8_t event_count;
    uint8_t task_count;
    uint8_t proposal_count;
    aura_sync_event_t events[AURA_SYNC_MAX_EVENTS];
    aura_sync_task_t tasks[AURA_SYNC_MAX_TASKS];
    aura_sync_proposal_t proposals[AURA_SYNC_MAX_PROPOSALS];
} aura_sync_snapshot_t;

esp_err_t aura_sync_init(void);
/** Wake the background worker for an immediate upload and digest refresh. */
void aura_sync_request_refresh(void);
void aura_sync_snapshot(aura_sync_snapshot_t *out);
esp_err_t aura_sync_refresh(void);
esp_err_t aura_sync_complete_task(int task_id);
esp_err_t aura_sync_delete_task(int task_id);
esp_err_t aura_sync_resolve_proposal(int proposal_id, bool approved);
const char *aura_sync_status_text(void);

#ifdef __cplusplus
}
#endif
