#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Start the AXP2101 monitor and audio worker. Safe to call once after BSP init. */
esp_err_t aura_hw_init(void);

/** Queue the built-in alarm chime. Returns immediately. */
esp_err_t aura_hw_play_chime(void);

/** Current software-pedometer count for the local calendar day. */
uint32_t aura_hw_steps(void);

typedef struct {
    bool storage_ready;
    bool recording;
    uint16_t queued_files;
    uint32_t captured_seconds;
    char status[48];
} aura_capture_status_t;

/** Current microphone/SD capture state. */
void aura_hw_capture_status(aura_capture_status_t *out);
void aura_hw_capture_rescan(void);

/** Shared duplex codec lock used by capture and chimes. */
bool aura_hw_audio_acquire(uint32_t timeout_ms);
void aura_hw_audio_release(void);

#ifdef __cplusplus
}
#endif
