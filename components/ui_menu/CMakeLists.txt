idf_component_register(
    SRCS    "ui_menu.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES gcode buzzer sdcard
    REQUIRES display encoder stepper spindle motion axis homing_state touch nvs_flash freertos
)
