idf_component_register(
    SRCS "nexus_touch.c"
    INCLUDE_DIRS "include"
    REQUIRES board esp_driver_gpio esp_driver_i2c
)
