idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES board display touch transport ui esp_system esp_timer
)
