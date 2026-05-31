idf_component_register(
    SRCS "touch.c" "touch_xpt2046.c" "touch_ft6x06.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_driver_spi esp_driver_i2c nvs_flash display
)
