idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES aura_core connectivity hardware notifications power_mgr ui esp_app_format
)
