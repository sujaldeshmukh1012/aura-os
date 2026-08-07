idf_component_register(
    SRCS "aura_wifi.c" "aura_sync.c"
    INCLUDE_DIRS "include"
    REQUIRES aura_core hardware esp_wifi esp_http_server esp_http_client esp_netif
             mbedtls espressif__cjson lwip
)
