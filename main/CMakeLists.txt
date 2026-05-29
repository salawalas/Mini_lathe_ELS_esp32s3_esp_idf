idf_component_register(
    SRCS        "main.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES gcode buzzer sdcard
    REQUIRES    display encoder axis limits stepper spindle ui_menu motion sdcard esp_system homing_state wifi_server ble_server
)
