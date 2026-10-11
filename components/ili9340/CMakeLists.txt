set(srcs "ili9340.c" "fontx.c")

idf_component_register(
	SRCS "ili9340.c" "fontx.c"
	INCLUDE_DIRS "."
	REQUIRES esp_driver_spi esp_driver_gpio
)
