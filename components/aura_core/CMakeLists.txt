idf_component_register(
    SRCS "aura_core.c" "aura_logic.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_event nvs_flash esp_timer driver esp_driver_i2c
)
