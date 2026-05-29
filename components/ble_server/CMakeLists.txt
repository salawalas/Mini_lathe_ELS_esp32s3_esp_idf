idf_component_register(
    SRCS "ble_server.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash bt axis stepper spindle homing_state
)
