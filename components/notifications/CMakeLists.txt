idf_component_register(
    SRCS "aura_notifications.c" "aura_ancs.c" "ble_ancs.c"
    INCLUDE_DIRS "include"
    REQUIRES aura_core bt esp_timer
)
