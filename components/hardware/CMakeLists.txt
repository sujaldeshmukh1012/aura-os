idf_component_register(
    SRCS "aura_hw.c" "aura_capture.c"
    INCLUDE_DIRS "include"
    REQUIRES aura_core waveshare__esp32_s3_touch_amoled_2_06 waveshare__qmi8658 esp_codec_dev fatfs
)
