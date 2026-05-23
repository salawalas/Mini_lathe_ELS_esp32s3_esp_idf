idf_component_register(
    SRCS "gcode.c"
    INCLUDE_DIRS "include"
    REQUIRES sdcard axis stepper spindle limits
)