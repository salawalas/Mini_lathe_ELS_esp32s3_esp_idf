idf_component_register(
    SRCS "wifi_server.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash esp_wifi esp_http_server axis stepper spindle homing_state
)
