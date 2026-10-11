idf_component_register(
    SRCS        "spindle.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_gptimer esp_driver_gpio esp_rom esp_timer freertos
)
