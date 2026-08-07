#include "aura_hw.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "aura_core.h"
#include "bsp/esp-bsp.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define SAMPLE_RATE          16000
#define BLOCK_SAMPLES        320
#define BLOCK_BYTES          (BLOCK_SAMPLES * sizeof(int16_t))
#define BLOCKS_PER_SECOND    (SAMPLE_RATE / BLOCK_SAMPLES)
#define STEALTH_SECONDS      (5 * 60)
#define STEALTH_MAX_BYTES    (BLOCK_BYTES * BLOCKS_PER_SECOND * STEALTH_SECONDS)
#define COPY_CHUNK_BYTES     512
#define OUTBOX_DIR           BSP_SD_MOUNT_POINT "/aura/outbox"
#define STEALTH_PATH         BSP_SD_MOUNT_POINT "/aura/stealth-ring.pcm"
#define ACTIVE_PATH          BSP_SD_MOUNT_POINT "/aura/capture-active.pcm.part"

static const char *TAG = "aura_capture";
static aura_capture_status_t s_status;
static SemaphoreHandle_t s_status_lock;
static uint16_t count_outbox(void);

static void publish_status(const char *text)
{
    xSemaphoreTake(s_status_lock, portMAX_DELAY);
    /* Avoid posting and rebuilding the Capture UI four times per second while
     * the mode is off. NULL explicitly means another status field changed. */
    if (text && strcmp(s_status.status, text) == 0) {
        xSemaphoreGive(s_status_lock);
        return;
    }
    if (text) strlcpy(s_status.status, text, sizeof(s_status.status));
    aura_capture_status_t copy = s_status;
    xSemaphoreGive(s_status_lock);
    (void)aura_post(AURA_EVT_CAPTURE_CHANGED, &copy, sizeof(copy));
}

void aura_hw_capture_status(aura_capture_status_t *out)
{
    if (!out) return;
    if (!s_status_lock) {
        memset(out, 0, sizeof(*out));
        strlcpy(out->status, "Capture starting", sizeof(out->status));
        return;
    }
    xSemaphoreTake(s_status_lock, portMAX_DELAY);
    *out = s_status;
    xSemaphoreGive(s_status_lock);
}

void aura_hw_capture_rescan(void)
{
    if (!s_status_lock) return;
    xSemaphoreTake(s_status_lock, portMAX_DELAY);
    s_status.queued_files = s_status.storage_ready ? count_outbox() : 0;
    xSemaphoreGive(s_status_lock);
    publish_status(NULL);
}

static uint16_t count_outbox(void)
{
    uint16_t count = 0;
    DIR *dir = opendir(OUTBOX_DIR);
    if (!dir) return 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        size_t len = strlen(entry->d_name);
        if (len > 4 && strcmp(entry->d_name + len - 4, ".wav") == 0) ++count;
    }
    closedir(dir);
    return count;
}

static void le16(FILE *file, uint16_t value)
{
    uint8_t bytes[] = {(uint8_t)value, (uint8_t)(value >> 8)};
    fwrite(bytes, 1, sizeof(bytes), file);
}

static void le32(FILE *file, uint32_t value)
{
    uint8_t bytes[] = {(uint8_t)value, (uint8_t)(value >> 8),
                       (uint8_t)(value >> 16), (uint8_t)(value >> 24)};
    fwrite(bytes, 1, sizeof(bytes), file);
}

static void wav_header(FILE *file, uint32_t pcm_bytes)
{
    rewind(file);
    fwrite("RIFF", 1, 4, file); le32(file, pcm_bytes + 36);
    fwrite("WAVEfmt ", 1, 8, file); le32(file, 16); le16(file, 1);
    le16(file, 1); le32(file, SAMPLE_RATE); le32(file, SAMPLE_RATE * 2);
    le16(file, 2); le16(file, 16); fwrite("data", 1, 4, file); le32(file, pcm_bytes);
}

typedef struct {
    FILE *file;
    uint32_t write_pos;
    uint32_t valid_bytes;
    uint16_t blocks_since_flush;
} stealth_ring_t;

static void set_recording(bool recording)
{
    xSemaphoreTake(s_status_lock, portMAX_DELAY);
    s_status.recording = recording;
    xSemaphoreGive(s_status_lock);
}

static bool microphone_set_open(esp_codec_dev_handle_t microphone, bool open,
                                bool *currently_open)
{
    if (*currently_open == open) return true;
    if (!aura_hw_audio_acquire(1000)) return false;
    int result;
    if (open) {
        esp_codec_dev_sample_info_t format = {
            .bits_per_sample = 16, .channel = 1, .channel_mask = 0,
            .sample_rate = SAMPLE_RATE, .mclk_multiple = 0,
        };
        result = esp_codec_dev_open(microphone, &format);
        if (result == ESP_CODEC_DEV_OK)
            result = esp_codec_dev_set_in_gain(microphone, 30.0f);
    } else {
        result = esp_codec_dev_close(microphone);
    }
    aura_hw_audio_release();
    if (result != ESP_CODEC_DEV_OK) {
        ESP_LOGW(TAG, "microphone %s failed: %d", open ? "open" : "close", result);
        return false;
    }
    *currently_open = open;
    ESP_LOGI(TAG, "microphone %s", open ? "active at 30 dB" : "hard-off");
    return true;
}

static void ring_discard(stealth_ring_t *ring)
{
    if (ring->file) fclose(ring->file);
    remove(STEALTH_PATH);
    memset(ring, 0, sizeof(*ring));
}

static bool ring_open(stealth_ring_t *ring)
{
    if (ring->file) return true;
    ring->file = fopen(STEALTH_PATH, "w+b");
    if (!ring->file) {
        ESP_LOGE(TAG, "could not create stealth ring: errno=%d", errno);
        return false;
    }
    ring->write_pos = ring->valid_bytes = ring->blocks_since_flush = 0;
    return true;
}

static bool ring_write(stealth_ring_t *ring, const int16_t *samples)
{
    if (!ring_open(ring)) return false;
    if (fwrite(samples, 1, BLOCK_BYTES, ring->file) != BLOCK_BYTES) return false;
    ring->write_pos += BLOCK_BYTES;
    if (ring->valid_bytes < STEALTH_MAX_BYTES) ring->valid_bytes += BLOCK_BYTES;
    if (ring->write_pos >= STEALTH_MAX_BYTES) {
        ring->write_pos = 0;
        if (fseek(ring->file, 0, SEEK_SET) != 0) return false;
        ESP_LOGI(TAG, "stealth ring wrapped; retaining latest five minutes");
    }
    if (++ring->blocks_since_flush >= BLOCKS_PER_SECOND) {
        fflush(ring->file);
        ring->blocks_since_flush = 0;
    }
    return true;
}

static bool copy_range(FILE *source, FILE *destination, uint32_t offset,
                       uint32_t length, uint8_t *buffer)
{
    if (length == 0) return true;
    if (fseek(source, offset, SEEK_SET) != 0) return false;
    while (length > 0) {
        size_t chunk = length < COPY_CHUNK_BYTES ? length : COPY_CHUNK_BYTES;
        if (fread(buffer, 1, chunk, source) != chunk ||
            fwrite(buffer, 1, chunk, destination) != chunk)
            return false;
        length -= chunk;
    }
    return true;
}

static bool assemble_capture(stealth_ring_t *ring, uint32_t forward_bytes,
                             time_t started_at)
{
    char final_path[160], part_path[176];
    snprintf(final_path, sizeof(final_path), OUTBOX_DIR "/capture-%lld.wav",
             (long long)started_at);
    snprintf(part_path, sizeof(part_path), "%s.part", final_path);
    FILE *output = fopen(part_path, "wb+");
    FILE *forward = fopen(ACTIVE_PATH, "rb");
    uint8_t *buffer = heap_caps_aligned_alloc(4, COPY_CHUNK_BYTES,
                                               MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!output || (forward_bytes && !forward) || !buffer) {
        if (output) fclose(output);
        if (forward) fclose(forward);
        free(buffer);
        remove(part_path);
        return false;
    }

    uint8_t empty[44] = {0};
    bool ok = fwrite(empty, 1, sizeof(empty), output) == sizeof(empty);
    if (ring->file && ring->valid_bytes) {
        fflush(ring->file);
        if (ring->valid_bytes == STEALTH_MAX_BYTES) {
            ok = ok && copy_range(ring->file, output, ring->write_pos,
                                  STEALTH_MAX_BYTES - ring->write_pos, buffer);
            ok = ok && copy_range(ring->file, output, 0, ring->write_pos, buffer);
        } else {
            ok = ok && copy_range(ring->file, output, 0, ring->valid_bytes, buffer);
        }
    }
    if (forward_bytes)
        ok = ok && copy_range(forward, output, 0, forward_bytes, buffer);

    uint32_t total_bytes = ring->valid_bytes + forward_bytes;
    if (ok) {
        wav_header(output, total_bytes);
        ok = fflush(output) == 0;
    }
    fclose(output);
    if (forward) fclose(forward);
    free(buffer);
    remove(ACTIVE_PATH);

    if (!ok || total_bytes == 0) {
        remove(part_path);
        return false;
    }
    if (rename(part_path, final_path) != 0) {
        ESP_LOGE(TAG, "capture rename failed: errno=%d", errno);
        remove(part_path);
        return false;
    }

    uint32_t seconds = total_bytes / (SAMPLE_RATE * sizeof(int16_t));
    xSemaphoreTake(s_status_lock, portMAX_DELAY);
    s_status.recording = false;
    s_status.captured_seconds = seconds;
    s_status.queued_files = count_outbox();
    xSemaphoreGive(s_status_lock);
    ESP_LOGI(TAG, "capture saved: %lu pre-roll + %lu forward bytes, %lu sec, queue=%u",
             (unsigned long)ring->valid_bytes, (unsigned long)forward_bytes,
             (unsigned long)seconds, (unsigned)count_outbox());
    publish_status("Saved; stealth resumed");
    return true;
}

static void capture_task(void *argument)
{
    (void)argument;
    esp_err_t mount = bsp_sdcard_mount();
    if (mount != ESP_OK && mount != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "microSD unavailable: %s", esp_err_to_name(mount));
        publish_status("Check FAT32 microSD");
        vTaskDelete(NULL);
        return;
    }
    (void)mkdir(BSP_SD_MOUNT_POINT "/aura", 0775);
    (void)mkdir(OUTBOX_DIR, 0775);
    xSemaphoreTake(s_status_lock, portMAX_DELAY);
    s_status.storage_ready = true;
    s_status.queued_files = count_outbox();
    xSemaphoreGive(s_status_lock);
    publish_status("Capture ready");

    esp_codec_dev_handle_t microphone = bsp_audio_codec_microphone_init();
    if (!microphone) {
        publish_status("Microphone unavailable");
        vTaskDelete(NULL);
        return;
    }
    int16_t samples[BLOCK_SAMPLES];
    stealth_ring_t ring = {0};
    FILE *active = NULL;
    uint32_t forward_bytes = 0;
    time_t capture_started_at = 0;
    bool microphone_open = false;
    aura_mode_t previous_mode = (aura_mode_t)-1;
    remove(STEALTH_PATH);
    remove(ACTIVE_PATH);

    for (;;) {
        aura_mode_t mode = aura_mode_get();
        if (mode != previous_mode) {
            if (previous_mode == AURA_MODE_CAPTURE) {
                if (active) {
                    fflush(active);
                    fclose(active);
                    active = NULL;
                }
                if (forward_bytes || ring.valid_bytes) {
                    publish_status("Saving capture");
                    if (!assemble_capture(&ring, forward_bytes, capture_started_at))
                        publish_status("Capture save failed");
                }
                set_recording(false);
                ring_discard(&ring);
                remove(ACTIVE_PATH);
                forward_bytes = 0;
            }

            if (mode == AURA_MODE_SILENT) {
                ring_discard(&ring);
                (void)microphone_set_open(microphone, false, &microphone_open);
                set_recording(false);
                publish_status("Silent · microphone off");
            } else if (mode == AURA_MODE_STEALTH) {
                if (!microphone_set_open(microphone, true, &microphone_open) ||
                    !ring_open(&ring))
                    publish_status("Stealth start failed");
                else
                    publish_status("Stealth · rolling 5 min");
            } else if (mode == AURA_MODE_CAPTURE) {
                if (!microphone_set_open(microphone, true, &microphone_open)) {
                    publish_status("Microphone open failed");
                } else {
                    capture_started_at = time(NULL);
                    active = fopen(ACTIVE_PATH, "wb+");
                    forward_bytes = 0;
                    if (!active) {
                        publish_status("SD write failed");
                    } else {
                        set_recording(true);
                        publish_status("Capture · preserving audio");
                        ESP_LOGI(TAG, "capture started with %lu bytes of stealth pre-roll",
                                 (unsigned long)ring.valid_bytes);
                    }
                }
            } else {
                (void)microphone_set_open(microphone, false, &microphone_open);
            }
            previous_mode = mode;
        }

        if (mode == AURA_MODE_SILENT || mode == AURA_MODE_CONVERSATION ||
            !microphone_open) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        if (!aura_hw_audio_acquire(1000)) continue;
        int rc = esp_codec_dev_read(microphone, samples, sizeof(samples));
        aura_hw_audio_release();
        if (rc != ESP_CODEC_DEV_OK) {
            publish_status("Microphone read failed");
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        if (mode == AURA_MODE_STEALTH) {
            if (!ring_write(&ring, samples)) {
                ring_discard(&ring);
                publish_status("Stealth SD write failed");
                vTaskDelay(pdMS_TO_TICKS(500));
            }
        } else if (mode == AURA_MODE_CAPTURE && active) {
            if (fwrite(samples, 1, BLOCK_BYTES, active) != BLOCK_BYTES) {
                fclose(active);
                active = NULL;
                set_recording(false);
                publish_status("SD full or removed");
            } else {
                forward_bytes += BLOCK_BYTES;
                if ((forward_bytes / BLOCK_BYTES) % BLOCKS_PER_SECOND == 0)
                    fflush(active);
            }
        }
    }
}

esp_err_t aura_capture_start(void)
{
    s_status_lock = xSemaphoreCreateMutex();
    if (!s_status_lock) return ESP_ERR_NO_MEM;
    strlcpy(s_status.status, "Capture starting", sizeof(s_status.status));
    return xTaskCreate(capture_task, "aura_capture", 6144, NULL, 4, NULL) == pdPASS
               ? ESP_OK : ESP_ERR_NO_MEM;
}
