#include "aura_sync.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>

#include "aura_core.h"
#include "aura_wifi.h"
#include "aura_hw.h"
#include "cJSON.h"
#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define CACHE_MAGIC 0x41555233U
#define CACHE_VERSION 2U
#define RESPONSE_CAPACITY 12288
#define UPLOAD_CHUNK_SIZE 512
#define POLL_INTERVAL_MS (10U * 60U * 1000U)
#define OUTBOX_DIR "/sdcard/aura/outbox"

typedef struct {
    uint32_t magic;
    uint16_t version;
    aura_sync_snapshot_t snapshot;
} cache_record_t;

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} response_buffer_t;

static const char *TAG = "aura_sync";
static aura_sync_snapshot_t s_snapshot;
static SemaphoreHandle_t s_lock;
static bool s_refreshing;
static TaskHandle_t s_refresh_task;

static esp_err_t aura_sync_upload_pending(void);

/* An explicit capture must not contend with TLS while it is being preserved.
 * Stealth uses small continuous SD writes and permits queued uploads so the
 * always-on rolling buffer does not prevent the outbox from ever draining. */
static bool audio_mode_active(void)
{
    aura_mode_t mode = aura_mode_get();
    return mode == AURA_MODE_CAPTURE || mode == AURA_MODE_CONVERSATION;
}

static void copy_json_string(char *out, size_t out_size, const cJSON *object,
                             const char *name)
{
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(object, name);
    strlcpy(out, cJSON_IsString(value) ? value->valuestring : "", out_size);
}

static esp_err_t http_event(esp_http_client_event_t *event)
{
    response_buffer_t *response = event->user_data;
    if (event->event_id != HTTP_EVENT_ON_DATA || !response || !response->data ||
        response->capacity == 0 || event->data_len <= 0)
        return ESP_OK;
    size_t available = response->capacity - response->length - 1;
    size_t copy = (size_t)event->data_len < available ? (size_t)event->data_len : available;
    memcpy(response->data + response->length, event->data, copy);
    response->length += copy;
    response->data[response->length] = '\0';
    return copy == (size_t)event->data_len ? ESP_OK : ESP_ERR_NO_MEM;
}

static esp_err_t server_credentials(char *base, size_t base_len,
                                    char *token, size_t token_len)
{
    (void)aura_settings_get_str("server_url", base, base_len,
                                "https://aura.sujalbhakare.com");
    (void)aura_settings_get_str("server_token", token, token_len, "");
    return base[0] && token[0] ? ESP_OK : ESP_ERR_INVALID_STATE;
}

static esp_err_t request(const char *method, const char *path,
                         const char *body, response_buffer_t *response)
{
    char base[160], token[192], url[224];
    if (server_credentials(base, sizeof(base), token, sizeof(token)) != ESP_OK)
        return ESP_ERR_INVALID_STATE;
    snprintf(url, sizeof(url), "%s%s", base, path);

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 12000,
        .event_handler = http_event,
        .user_data = response,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 2048,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return ESP_ERR_NO_MEM;
    char authorization[224];
    snprintf(authorization, sizeof(authorization), "Bearer %s", token);
    esp_http_client_set_header(client, "Authorization", authorization);
    if (strcmp(method, "POST") == 0) {
        esp_http_client_set_method(client, HTTP_METHOD_POST);
        body = body ? body : "";
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, body, strlen(body));
    } else if (strcmp(method, "DELETE") == 0) {
        esp_http_client_set_method(client, HTTP_METHOD_DELETE);
    }
    esp_err_t err = esp_http_client_perform(client);
    int status = err == ESP_OK ? esp_http_client_get_status_code(client) : 0;
    esp_http_client_cleanup(client);
    if (err != ESP_OK) return err;
    return status >= 200 && status < 300 ? ESP_OK : ESP_FAIL;
}

static esp_err_t parse_digest(const char *json, aura_sync_snapshot_t *snapshot)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return ESP_ERR_INVALID_RESPONSE;
    const cJSON *schema = cJSON_GetObjectItemCaseSensitive(root, "schema");
    const cJSON *events = cJSON_GetObjectItemCaseSensitive(root, "events");
    const cJSON *tasks = cJSON_GetObjectItemCaseSensitive(root, "tasks");
    const cJSON *proposals = cJSON_GetObjectItemCaseSensitive(root, "proposals");
    if (!cJSON_IsNumber(schema) || schema->valueint != 1 ||
        !cJSON_IsArray(events) || !cJSON_IsArray(tasks)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->has_cache = true;
    snapshot->online = true;
    snapshot->updated_epoch = time(NULL);

    const cJSON *item;
    cJSON_ArrayForEach(item, events) {
        if (snapshot->event_count >= AURA_SYNC_MAX_EVENTS) break;
        aura_sync_event_t *event = &snapshot->events[snapshot->event_count++];
        const cJSON *id = cJSON_GetObjectItemCaseSensitive(item, "id");
        const cJSON *start = cJSON_GetObjectItemCaseSensitive(item, "start_epoch");
        const cJSON *all_day = cJSON_GetObjectItemCaseSensitive(item, "all_day");
        event->id = cJSON_IsNumber(id) ? id->valueint : 0;
        event->start_epoch = cJSON_IsNumber(start) ? (time_t)start->valuedouble : 0;
        event->all_day = cJSON_IsTrue(all_day) || (cJSON_IsNumber(all_day) && all_day->valueint);
        copy_json_string(event->title, sizeof(event->title), item, "title");
        copy_json_string(event->calendar, sizeof(event->calendar), item, "calendar_name");
        copy_json_string(event->location, sizeof(event->location), item, "location");
    }
    cJSON_ArrayForEach(item, tasks) {
        if (snapshot->task_count >= AURA_SYNC_MAX_TASKS) break;
        aura_sync_task_t *task = &snapshot->tasks[snapshot->task_count++];
        const cJSON *id = cJSON_GetObjectItemCaseSensitive(item, "id");
        const cJSON *priority = cJSON_GetObjectItemCaseSensitive(item, "priority");
        task->id = cJSON_IsNumber(id) ? id->valueint : 0;
        task->priority = cJSON_IsNumber(priority) ? priority->valueint : 0;
        copy_json_string(task->title, sizeof(task->title), item, "title");
        copy_json_string(task->project, sizeof(task->project), item, "project");
        copy_json_string(task->due_at, sizeof(task->due_at), item, "due_at");
    }
    if (cJSON_IsArray(proposals)) cJSON_ArrayForEach(item, proposals) {
        if (snapshot->proposal_count >= AURA_SYNC_MAX_PROPOSALS) break;
        aura_sync_proposal_t *proposal = &snapshot->proposals[snapshot->proposal_count++];
        const cJSON *id = cJSON_GetObjectItemCaseSensitive(item, "id");
        const cJSON *priority = cJSON_GetObjectItemCaseSensitive(item, "priority");
        proposal->id = cJSON_IsNumber(id) ? id->valueint : 0;
        proposal->priority = cJSON_IsNumber(priority) ? priority->valueint : 0;
        copy_json_string(proposal->kind, sizeof(proposal->kind), item, "kind");
        copy_json_string(proposal->title, sizeof(proposal->title), item, "title");
        copy_json_string(proposal->details, sizeof(proposal->details), item, "details");
        copy_json_string(proposal->due_at, sizeof(proposal->due_at), item, "due_at");
        copy_json_string(proposal->starts_at, sizeof(proposal->starts_at), item, "starts_at");
    }
    cJSON_Delete(root);
    return ESP_OK;
}

static void publish_snapshot(const aura_sync_snapshot_t *snapshot)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_snapshot = *snapshot;
    xSemaphoreGive(s_lock);
    cache_record_t *cache = malloc(sizeof(*cache));
    if (cache) {
        *cache = (cache_record_t){ .magic = CACHE_MAGIC, .version = CACHE_VERSION,
                                  .snapshot = *snapshot };
        cache->snapshot.online = false;
        (void)aura_settings_set_blob("digest_v1", cache, sizeof(*cache));
        free(cache);
    }
    (void)aura_post(AURA_EVT_DIGEST_UPDATED, NULL, 0);
}

static esp_err_t upload_file(const char *path, const char *filename)
{
    if (audio_mode_active()) return ESP_ERR_INVALID_STATE;
    struct stat info;
    if (stat(path, &info) != 0 || info.st_size <= 44) return ESP_ERR_INVALID_SIZE;
    char base[160], token[192], url[224];
    if (server_credentials(base, sizeof(base), token, sizeof(token)) != ESP_OK)
        return ESP_ERR_INVALID_STATE;
    snprintf(url, sizeof(url), "%s/api/v1/audio", base);
    esp_http_client_config_t config = {
        .url = url, .method = HTTP_METHOD_POST, .timeout_ms = 45000,
        .crt_bundle_attach = esp_crt_bundle_attach, .buffer_size = 2048,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return ESP_ERR_NO_MEM;
    char authorization[224];
    snprintf(authorization, sizeof(authorization), "Bearer %s", token);
    esp_http_client_set_header(client, "Authorization", authorization);
    esp_http_client_set_header(client, "Content-Type", "audio/wav");
    esp_http_client_set_header(client, "X-Aura-Device", "aura-watch");
    esp_http_client_set_header(client, "X-Aura-Filename", filename);
    char duration[24];
    double seconds = (double)(info.st_size - 44) / (16000.0 * 2.0);
    snprintf(duration, sizeof(duration), "%.2f", seconds);
    esp_http_client_set_header(client, "X-Aura-Duration", duration);
    long long captured_epoch = 0;
    if (sscanf(filename, "capture-%lld.wav", &captured_epoch) == 1 &&
        captured_epoch > 0) {
        time_t captured = (time_t)captured_epoch;
        struct tm utc;
        char captured_at[32];
        if (gmtime_r(&captured, &utc) &&
            strftime(captured_at, sizeof(captured_at), "%Y-%m-%dT%H:%M:%SZ", &utc))
            esp_http_client_set_header(client, "X-Aura-Captured-At", captured_at);
    }

    FILE *file = fopen(path, "rb");
    if (!file) {
        esp_http_client_cleanup(client);
        return ESP_ERR_NOT_FOUND;
    }
    /* Avoid a second stdio buffer and keep each SDMMC DMA request to one
     * 512-byte sector. This matters on the ESP32-S3 once BLE and Wi-Fi run. */
    (void)setvbuf(file, NULL, _IONBF, 0);
    esp_err_t err = esp_http_client_open(client, info.st_size);
    /* Feed FatFS an aligned, explicitly DMA-capable destination so SDMMC can
     * read directly instead of allocating a temporary bounce buffer. */
    uint8_t *buffer = heap_caps_aligned_alloc(4, UPLOAD_CHUNK_SIZE,
                                               MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (err == ESP_OK && !buffer) err = ESP_ERR_NO_MEM;
    while (err == ESP_OK) {
        /* Capture may have been enabled while TLS was connecting. Abort the
         * partial request promptly; the intact file is retried after capture. */
        if (audio_mode_active()) {
            err = ESP_ERR_INVALID_STATE;
            break;
        }
        size_t read = fread(buffer, 1, UPLOAD_CHUNK_SIZE, file);
        if (read == 0) break;
        int written = esp_http_client_write(client, (const char *)buffer, read);
        if (written != (int)read) err = ESP_FAIL;
    }
    free(buffer);
    fclose(file);
    int status = 0;
    if (err == ESP_OK) {
        (void)esp_http_client_fetch_headers(client);
        status = esp_http_client_get_status_code(client);
        if (status < 200 || status >= 300) err = ESP_FAIL;
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (err == ESP_OK) {
        if (remove(path) != 0) return ESP_FAIL;
        aura_hw_capture_rescan();
        ESP_LOGI(TAG, "uploaded capture %s", filename);
    } else if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGI(TAG, "capture upload deferred while audio is active");
    } else {
        ESP_LOGW(TAG, "capture upload failed (%d): %s", status, esp_err_to_name(err));
    }
    return err;
}

static esp_err_t aura_sync_upload_pending(void)
{
    if (audio_mode_active()) return ESP_ERR_INVALID_STATE;
    DIR *dir = opendir(OUTBOX_DIR);
    if (!dir) return ESP_ERR_NOT_FOUND;
    esp_err_t result = ESP_OK;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (audio_mode_active()) {
            result = ESP_ERR_INVALID_STATE;
            break;
        }
        size_t length = strlen(entry->d_name);
        if (length <= 4 || strcmp(entry->d_name + length - 4, ".wav") != 0) continue;
        char path[320];
        snprintf(path, sizeof(path), OUTBOX_DIR "/%s", entry->d_name);
        result = upload_file(path, entry->d_name);
        if (result != ESP_OK) break;
    }
    closedir(dir);
    return result;
}

esp_err_t aura_sync_refresh(void)
{
    if (aura_wifi_state() != AURA_WIFI_CONNECTED || audio_mode_active())
        return ESP_ERR_INVALID_STATE;
    char *body = calloc(1, RESPONSE_CAPACITY);
    if (!body) return ESP_ERR_NO_MEM;
    response_buffer_t response = { .data = body, .capacity = RESPONSE_CAPACITY };
    esp_err_t err = request("GET", "/api/v1/digest", NULL, &response);
    aura_sync_snapshot_t *next = calloc(1, sizeof(*next));
    if (!next) {
        free(body);
        return ESP_ERR_NO_MEM;
    }
    if (err == ESP_OK) err = parse_digest(body, next);
    free(body);
    if (err == ESP_OK) {
        publish_snapshot(next);
        ESP_LOGI(TAG, "digest updated: %u events, %u tasks",
                 next->event_count, next->task_count);
    } else {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_snapshot.online = false;
        xSemaphoreGive(s_lock);
        (void)aura_post(AURA_EVT_DIGEST_UPDATED, NULL, 0);
        ESP_LOGW(TAG, "digest refresh failed: %s", esp_err_to_name(err));
    }
    free(next);
    return err;
}

static void refresh_task(void *argument)
{
    (void)argument;
    for (;;) {
        bool connected = aura_wifi_state() == AURA_WIFI_CONNECTED;
        if (connected && !audio_mode_active() && !s_refreshing) {
            s_refreshing = true;
            (void)aura_sync_upload_pending();
            (void)aura_sync_refresh();
            s_refreshing = false;
        }
        /* A capture or configuration change can wake this wait immediately.
         * The periodic timeout remains as the offline reconnect/digest poll. */
        (void)ulTaskNotifyTake(pdTRUE,
                               pdMS_TO_TICKS(connected ? POLL_INTERVAL_MS : 5000));
    }
}

void aura_sync_request_refresh(void)
{
    if (s_refresh_task) xTaskNotifyGive(s_refresh_task);
}

static void capture_changed(void *argument, esp_event_base_t base,
                            int32_t id, void *data)
{
    (void)argument;
    (void)base;
    (void)id;
    const aura_capture_status_t *status = data;
    if (status && !status->recording && status->queued_files > 0)
        aura_sync_request_refresh();
}

static void mode_changed(void *argument, esp_event_base_t base,
                         int32_t id, void *data)
{
    (void)argument;
    (void)base;
    (void)id;
    aura_mode_t mode = data ? *(const aura_mode_t *)data : aura_mode_get();
    if (mode == AURA_MODE_SILENT || mode == AURA_MODE_STEALTH)
        aura_sync_request_refresh();
}

typedef struct { int id; } complete_args_t;

static void complete_task(void *argument)
{
    complete_args_t args = *(complete_args_t *)argument;
    free(argument);
    char path[64];
    snprintf(path, sizeof(path), "/api/v1/tasks/%d/complete", args.id);
    response_buffer_t response = {0};
    if (request("POST", path, NULL, &response) == ESP_OK) (void)aura_sync_refresh();
    vTaskDelete(NULL);
}

static void delete_task(void *argument)
{
    complete_args_t args = *(complete_args_t *)argument;
    free(argument);
    char path[64];
    snprintf(path, sizeof(path), "/api/v1/tasks/%d", args.id);
    response_buffer_t response = {0};
    if (request("DELETE", path, NULL, &response) == ESP_OK)
        (void)aura_sync_refresh();
    vTaskDelete(NULL);
}

typedef struct { int id; bool approved; } proposal_args_t;

static void resolve_proposal_task(void *argument)
{
    proposal_args_t args = *(proposal_args_t *)argument;
    free(argument);
    char path[80];
    snprintf(path, sizeof(path), "/api/v1/proposals/%d/resolve", args.id);
    const char *body = args.approved ? "{\"approved\":true}" : "{\"approved\":false}";
    response_buffer_t response = {0};
    if (request("POST", path, body, &response) == ESP_OK) (void)aura_sync_refresh();
    vTaskDelete(NULL);
}

esp_err_t aura_sync_resolve_proposal(int proposal_id, bool approved)
{
    proposal_args_t *args = malloc(sizeof(*args));
    if (!args) return ESP_ERR_NO_MEM;
    *args = (proposal_args_t){ .id = proposal_id, .approved = approved };
    if (xTaskCreate(resolve_proposal_task, "memory_review", 8192, args, 5, NULL) != pdPASS) {
        free(args);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t aura_sync_complete_task(int task_id)
{
    complete_args_t *args = malloc(sizeof(*args));
    if (!args) return ESP_ERR_NO_MEM;
    args->id = task_id;
    if (xTaskCreate(complete_task, "task_done", 8192, args, 5, NULL) != pdPASS) {
        free(args);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t aura_sync_delete_task(int task_id)
{
    complete_args_t *args = malloc(sizeof(*args));
    if (!args) return ESP_ERR_NO_MEM;
    args->id = task_id;
    if (xTaskCreate(delete_task, "task_delete", 8192, args, 5, NULL) != pdPASS) {
        free(args);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void aura_sync_snapshot(aura_sync_snapshot_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_snapshot;
    xSemaphoreGive(s_lock);
}

const char *aura_sync_status_text(void)
{
    return s_snapshot.online ? "Synced" : s_snapshot.has_cache ? "Offline cache" : "Not configured";
}

esp_err_t aura_sync_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;
    cache_record_t *cache = malloc(sizeof(*cache));
    if (!cache) return ESP_ERR_NO_MEM;
    size_t length = sizeof(*cache);
    if (aura_settings_get_blob("digest_v1", cache, &length) == ESP_OK &&
        length == sizeof(*cache) && cache->magic == CACHE_MAGIC &&
        cache->version == CACHE_VERSION) {
        s_snapshot = cache->snapshot;
        s_snapshot.online = false;
    }
    free(cache);
    ESP_RETURN_ON_ERROR(aura_subscribe(AURA_EVT_CAPTURE_CHANGED, capture_changed, NULL),
                        TAG, "capture event subscription");
    ESP_RETURN_ON_ERROR(aura_subscribe(AURA_EVT_MODE_CHANGED, mode_changed, NULL),
                        TAG, "mode event subscription");
    return xTaskCreate(refresh_task, "aura_sync", 7168, NULL, 4,
                       &s_refresh_task) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
