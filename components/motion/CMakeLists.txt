idf_component_register(
    SRCS        "motion.c"
    INCLUDE_DIRS "include"
    REQUIRES    driver stepper spindle display esp_timer freertos
)
