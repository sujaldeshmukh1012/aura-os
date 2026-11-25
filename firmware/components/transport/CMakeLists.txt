idf_component_register(
    SRCS "nexus_transport.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_usb_serial_jtag
)
