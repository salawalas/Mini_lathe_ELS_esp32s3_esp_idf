idf_component_register(
    SRCS        "display.c"
    INCLUDE_DIRS "include"
    REQUIRES    nopnop2002__ili9340 driver spiffs freertos
)
