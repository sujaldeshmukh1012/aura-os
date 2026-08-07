idf_component_register(
    SRCS "power_mgr.c"
    INCLUDE_DIRS "include"
    REQUIRES aura_core esp_timer driver esp_common
)
