idf_component_register(
    SRCS "nexus_display.c"
    INCLUDE_DIRS "include"
    REQUIRES board esp_driver_gpio esp_driver_spi
)
