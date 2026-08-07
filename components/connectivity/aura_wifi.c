#include "aura_wifi.h"
#include "aura_sync.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "aura_core.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "aura_wifi";

#define CONNECT_RETRIES     4
#define SNTP_REFRESH_MS     (15U * 60U * 1000U)

static aura_wifi_state_t s_state = AURA_WIFI_OFF;
static httpd_handle_t s_http;
static bool s_sntp_started;
static bool s_time_synced;
static unsigned s_retries;
static char s_ip[16] = "--";
static char s_ssid[33] = "--";

static esp_err_t connect_saved(void);

static void set_state(aura_wifi_state_t state)
{
    s_state = state;
    (void)aura_post(AURA_EVT_SETTINGS_CHANGED, NULL, 0);
}

aura_wifi_state_t aura_wifi_state(void) { return s_state; }
bool aura_wifi_setup_hotspot_active(void) { return s_state == AURA_WIFI_PROVISIONING; }
bool aura_wifi_time_is_synced(void) { return s_time_synced; }
const char *aura_wifi_ip(void) { return s_ip; }
const char *aura_wifi_ssid(void) { return s_ssid; }

const char *aura_wifi_status_text(void)
{
    switch (s_state) {
    case AURA_WIFI_CONNECTING: return "Wi-Fi: connecting";
    case AURA_WIFI_CONNECTED: return "Wi-Fi: connected";
    case AURA_WIFI_PROVISIONING: return "Setup hotspot: ON";
    case AURA_WIFI_FAILED: return "Wi-Fi: connection failed";
    default: return "Wi-Fi: off";
    }
}

static void time_sync_cb(struct timeval *tv)
{
    (void)tv;
    struct tm local;
    aura_time_now(&local);
    esp_err_t err = aura_time_set(&local);
    if (err == ESP_OK) {
        s_time_synced = true;
        (void)aura_post(AURA_EVT_SETTINGS_CHANGED, NULL, 0);
        ESP_LOGI(TAG, "network time saved to RTC");
    } else {
        ESP_LOGW(TAG, "RTC update failed: %s", esp_err_to_name(err));
    }
}

static void refresh_network_time(void)
{
    if (!s_sntp_started) {
        esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, "time.google.com");
        esp_sntp_set_sync_interval(SNTP_REFRESH_MS);
        esp_sntp_set_time_sync_notification_cb(time_sync_cb);
        esp_sntp_init();
        s_sntp_started = true;
    } else {
        /* Reconnecting should fetch current time now, not wait for the next
         * periodic interval. */
        (void)esp_sntp_restart();
    }
}

static const char SETUP_HTML[] =
    "<!doctype html><meta name=viewport content='width=device-width'>"
    "<style>body{font:18px system-ui;max-width:420px;margin:40px auto;padding:20px;background:#080808;color:#eee}"
    "input,button{box-sizing:border-box;width:100%;font-size:18px;padding:14px;margin:8px 0;border-radius:10px}"
    "button{background:#38b6ff;border:0}</style><h1>Aura Wi-Fi</h1>"
    "<p>Save the network this watch should use for uploads and sync.</p>"
    "<form method=post action=/save><input name=ssid maxlength=32 placeholder='Wi-Fi name' required>"
    "<input name=pass maxlength=64 type=password placeholder='Password'>"
    "<button>Save fallback and connect</button></form><hr>"
    "<h2>Aura Server</h2><p>Configure this after the server has its permanent HTTPS address.</p>"
    "<form method=post action=/server><input name=server maxlength=159 placeholder='https://aura.example.com' required>"
    "<input name=token maxlength=191 type=password placeholder='API token' required>"
    "<button>Save server</button></form>";

static esp_err_t setup_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, SETUP_HTML, HTTPD_RESP_USE_STRLEN);
}

static void url_decode(char *out, size_t out_len, const char *in)
{
    size_t j = 0;
    for (size_t i = 0; in[i] && j + 1 < out_len; ++i) {
        if (in[i] == '+') out[j++] = ' ';
        else if (in[i] == '%' && isxdigit((unsigned char)in[i + 1]) && isxdigit((unsigned char)in[i + 2])) {
            unsigned value;
            sscanf(in + i + 1, "%2x", &value);
            out[j++] = (char)value;
            i += 2;
        } else out[j++] = in[i];
    }
    out[j] = '\0';
}

static bool form_value(const char *body, const char *key, char *out, size_t out_len)
{
    char needle[20];
    snprintf(needle, sizeof(needle), "%s=", key);
    const char *start = strstr(body, needle);
    if (!start) return false;
    start += strlen(needle);
    const char *end = strchr(start, '&');
    size_t len = end ? (size_t)(end - start) : strlen(start);
    char encoded[256];
    if (len >= sizeof(encoded)) return false;
    memcpy(encoded, start, len);
    encoded[len] = '\0';
    url_decode(out, out_len, encoded);
    return true;
}

static esp_err_t start_web_server(void);

static esp_err_t connect_credentials(const char *ssid, const char *password)
{
    wifi_config_t config = {0};
    strlcpy((char *)config.sta.ssid, ssid, sizeof(config.sta.ssid));
    strlcpy((char *)config.sta.password, password, sizeof(config.sta.password));
    config.sta.threshold.authmode = password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    config.sta.pmf_cfg.capable = true;

    if (s_http) {
        httpd_stop(s_http);
        s_http = NULL;
    }
    (void)esp_wifi_stop();
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "station mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &config), TAG, "station config");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "station start");
    s_retries = 0;
    strlcpy(s_ssid, ssid, sizeof(s_ssid));
    strlcpy(s_ip, "--", sizeof(s_ip));
    set_state(AURA_WIFI_CONNECTING);
    ESP_LOGI(TAG, "connecting to saved network %s", ssid);
    return esp_wifi_connect();
}

static esp_err_t connect_saved(void)
{
    char ssid[33], password[65];
    (void)aura_settings_get_str("wifi_ssid", ssid, sizeof(ssid), "");
    (void)aura_settings_get_str("wifi_pass", password, sizeof(password), "");
    if (ssid[0] == '\0') return ESP_ERR_NOT_FOUND;
    return connect_credentials(ssid, password);
}

static void apply_saved_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(250));
    (void)connect_saved();
    vTaskDelete(NULL);
}

static esp_err_t setup_post(httpd_req_t *req)
{
    if (req->content_len <= 0 || req->content_len >= 192) return ESP_FAIL;
    char body[192] = {0};
    int received = httpd_req_recv(req, body, req->content_len);
    if (received <= 0) return ESP_FAIL;
    body[received] = '\0';
    char ssid[33] = {0}, password[65] = {0};
    if (!form_value(body, "ssid", ssid, sizeof(ssid)) || ssid[0] == '\0' ||
        !form_value(body, "pass", password, sizeof(password))) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid credentials");
    }
    (void)aura_settings_set_str("wifi_ssid", ssid);
    (void)aura_settings_set_str("wifi_pass", password);
    httpd_resp_sendstr(req, "Saved. The watch is connecting to the fallback network.");
    return xTaskCreate(apply_saved_task, "wifi_apply", 3072, NULL, 5, NULL) == pdPASS
           ? ESP_OK : ESP_ERR_NO_MEM;
}

static esp_err_t server_post(httpd_req_t *req)
{
    if (req->content_len <= 0 || req->content_len >= 512) return ESP_FAIL;
    char body[512] = {0};
    int received = httpd_req_recv(req, body, req->content_len);
    if (received <= 0) return ESP_FAIL;
    body[received] = '\0';
    char server[160] = {0}, token[192] = {0};
    if (!form_value(body, "server", server, sizeof(server)) ||
        !form_value(body, "token", token, sizeof(token)) ||
        strncmp(server, "https://", 8) != 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "Use a valid HTTPS server URL and token");
    }
    size_t len = strlen(server);
    while (len > 8 && server[len - 1] == '/') server[--len] = '\0';
    esp_err_t err = aura_settings_set_str("server_url", server);
    if (err == ESP_OK) err = aura_settings_set_str("server_token", token);
    if (err != ESP_OK)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "Could not save configuration");
    (void)aura_post(AURA_EVT_SETTINGS_CHANGED, NULL, 0);
    aura_sync_request_refresh();
    return httpd_resp_sendstr(req, "Aura Server saved. Sync started.");
}

static esp_err_t start_web_server(void)
{
    if (s_http) return ESP_OK;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    /* This server only hosts three small configuration handlers. The default
     * 4 KiB task and socket count needlessly compete with display DMA, audio,
     * Wi-Fi and NimBLE for scarce internal RAM. */
    config.stack_size = 3072;
    config.max_open_sockets = 3;
    config.lru_purge_enable = true;
    ESP_RETURN_ON_ERROR(httpd_start(&s_http, &config), TAG, "web setup start");
    const httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler = setup_get };
    const httpd_uri_t save = { .uri = "/save", .method = HTTP_POST, .handler = setup_post };
    const httpd_uri_t server = { .uri = "/server", .method = HTTP_POST, .handler = server_post };
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_http, &root), TAG, "web root");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_http, &save), TAG, "Wi-Fi save");
    return httpd_register_uri_handler(s_http, &server);
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED &&
        s_state != AURA_WIFI_PROVISIONING) {
        if (++s_retries <= CONNECT_RETRIES) {
            set_state(AURA_WIFI_CONNECTING);
            (void)esp_wifi_connect();
        } else {
            set_state(AURA_WIFI_FAILED);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&event->ip_info.ip));
        s_retries = 0;
        set_state(AURA_WIFI_CONNECTED);
        refresh_network_time();
        esp_err_t err = start_web_server();
        if (err != ESP_OK) ESP_LOGW(TAG, "LAN setup page unavailable: %s", esp_err_to_name(err));
        aura_sync_request_refresh();
        ESP_LOGI(TAG, "setup page: http://%s", s_ip);
    }
}

esp_err_t aura_wifi_start_provisioning(void)
{
    if (s_state == AURA_WIFI_PROVISIONING) return ESP_OK;
    /* Mark this as an intentional mode change before stopping STA.  The stop
     * can emit STA_DISCONNECTED, which must not race us by reconnecting. */
    set_state(AURA_WIFI_PROVISIONING);
    if (s_http) {
        httpd_stop(s_http);
        s_http = NULL;
    }
    (void)esp_wifi_stop();

    uint8_t mac[6];
    ESP_RETURN_ON_ERROR(esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP), TAG, "read MAC");
    wifi_config_t config = {0};
    snprintf((char *)config.ap.ssid, sizeof(config.ap.ssid), "Aura-Setup-%02X%02X", mac[4], mac[5]);
    config.ap.ssid_len = strlen((char *)config.ap.ssid);
    config.ap.channel = 1;
    config.ap.max_connection = 2;
    config.ap.authmode = WIFI_AUTH_OPEN;
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG, "AP mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &config), TAG, "AP config");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "AP start");
    strlcpy(s_ssid, (char *)config.ap.ssid, sizeof(s_ssid));
    strlcpy(s_ip, "192.168.4.1", sizeof(s_ip));
    ESP_RETURN_ON_ERROR(start_web_server(), TAG, "AP web setup");
    return ESP_OK;
}

esp_err_t aura_wifi_stop_provisioning(void)
{
    if (s_state != AURA_WIFI_PROVISIONING) return ESP_OK;
    return connect_saved();
}

esp_err_t aura_wifi_init(void)
{
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "Wi-Fi init");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL),
                        TAG, "Wi-Fi events");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL),
                        TAG, "IP events");
    err = connect_saved();
    if (err == ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "no saved Wi-Fi network; use Wi-Fi Setup from Settings");
        set_state(AURA_WIFI_FAILED);
        return ESP_OK;
    }
    return err;
}
