idf_component_register(
    SRCS        "sdcard.c"
    INCLUDE_DIRS "include"
    REQUIRES    esp_driver_sdspi esp_driver_spi fatfs sdmmc
)
