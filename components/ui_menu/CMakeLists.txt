idf_component_register(
    SRCS        "ui_menu.c"
    INCLUDE_DIRS "include"
    REQUIRES    display encoder stepper spindle motion axis freertos nvs_flash limits
)
