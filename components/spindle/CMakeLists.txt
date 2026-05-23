idf_component_register(
    SRCS        "spindle.c"
    INCLUDE_DIRS "include"
    REQUIRES    driver esp_timer freertos
)
