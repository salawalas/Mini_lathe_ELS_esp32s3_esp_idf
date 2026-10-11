idf_component_register(
    SRCS        "motion.c"
    INCLUDE_DIRS "include"
    REQUIRES stepper spindle esp_timer freertos
)
