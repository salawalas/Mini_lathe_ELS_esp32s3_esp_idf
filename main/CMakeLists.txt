idf_component_register(
    SRCS        "main.c"
    INCLUDE_DIRS "."
    REQUIRES    display encoder axis limits stepper spindle ui_menu motion sdcard esp_system
)
