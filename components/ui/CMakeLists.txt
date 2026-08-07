idf_component_register(
    SRCS "ui_shell.c" "tile_watchface.c" "tile_clock.c" "tile_connected.c"
         "tile_stubs.c" "tile_settings.c"
    INCLUDE_DIRS "include"
    REQUIRES aura_core connectivity hardware notifications power_mgr esp_timer
             mbedtls esp_hw_support
)
