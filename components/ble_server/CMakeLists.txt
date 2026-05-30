idf_component_register(
    SRCS "ble_server.c"
    INCLUDE_DIRS "include"
    REQUIRES bt nvs_flash axis stepper spindle homing_state
)
