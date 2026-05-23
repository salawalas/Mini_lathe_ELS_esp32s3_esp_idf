#pragma once
// ============================================================
//  sdcard.h  –  Karta SD przez SPI (współdzielona z TFT SPI2)
//  ESP32-S3-WROOM-1 N16R8  /  DevKitC-1
//  Optymalizacja v6
// ============================================================
//
//  Magistrala SPI2 (wspólna z TFT):
//    MOSI = IO11  (sdkconfig.defaults: CONFIG_MOSI_GPIO=11)
//    SCLK = IO12  (sdkconfig.defaults: CONFIG_SCLK_GPIO=12)
//    MISO = IO13  –  karta SD i TFT współdzielą MISO (lewa strona, sąsiad IO12)
//
//  IO44 = SD CS – wybrany jako wolny GPIO, bez konfliktu z LED (IO48)
//  ani strapping pins. IO43 też jest wolny jako alternatywa.
//
//  IO13 = MISO – sąsiad MOSI/SCLK na lewej stronie, idealna lokalizacja.

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#define SD_PIN_MISO     13    // lewa strona – sąsiad IO11(MOSI), IO12(SCLK)
#define SD_PIN_CS       44    // IO44 – wolny, brak konfliktu z LED czy strapping

esp_err_t sdcard_init(void);
bool      sdcard_is_mounted(void);

typedef void (*sdcard_file_cb_t)(const char *name, uint32_t size, void *arg);
void      sdcard_list_gcode_files(sdcard_file_cb_t cb, void *arg);
int       sdcard_read_file(const char *path, char *buf, int max_len);
void      sdcard_info(uint32_t *total_mb, uint32_t *free_mb);
