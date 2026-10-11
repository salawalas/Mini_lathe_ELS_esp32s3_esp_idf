idf_component_register(
    SRCS        "display.c"
    INCLUDE_DIRS "include"
    REQUIRES ili9340 esp_driver_ledc esp_driver_gpio spiffs freertos
)
