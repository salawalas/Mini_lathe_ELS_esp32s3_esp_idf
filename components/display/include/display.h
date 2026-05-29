#pragma once
// ============================================================
//  display.h  –  Wrapper nad biblioteką nopnop2002/ili9340
//  Mini Lathe Controller v4
//
//  GPIO i parametry wyświetlacza konfigurowane przez:
//  idf.py menuconfig → TFT Configuration
//
//  Żeby zmienić wyświetlacz na inny (np. ILI9341 320×240):
//    menuconfig → wybierz model → ustaw wymiary i offsety
//    Reszta kodu bez zmian!
// ============================================================

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "ili9340.h"
#include "fontx.h"

// ------------------------------------------------------------
//  Parametry wyświetlacza – ZGODNE Z menuconfig.
//  Zmieniaj przez: idf.py menuconfig -> TFT Configuration
// ------------------------------------------------------------
#if CONFIG_ILI9225
#define DISP_MODEL      0x9225
#elif CONFIG_ILI9340
#define DISP_MODEL      0x9340
#elif CONFIG_ILI9341
#define DISP_MODEL      0x9341
#elif CONFIG_ST7735
#define DISP_MODEL      0x7735
#elif CONFIG_ST7789
#define DISP_MODEL      0x7789
#elif CONFIG_ST7796
#define DISP_MODEL      0x7796
#else
#error "Select display driver in menuconfig -> TFT Configuration"
#endif

#define DISP_W          CONFIG_WIDTH
#define DISP_H          CONFIG_HEIGHT
#define DISP_OFFSET_X   CONFIG_OFFSETX
#define DISP_OFFSET_Y   CONFIG_OFFSETY
#define DISP_BL         CONFIG_BL_GPIO

// SPI clock set explicitly before spi_master_init().
#define DISP_SPI_FREQ_HZ (15 * 1000 * 1000)

// ------------------------------------------------------------
//  Rozmiary fontów FONTX
//  ILGH16XB = Gothic 8×16px
//  ILGH24XB = Gothic 12×24px
//  ILGH32XB = Gothic 16×32px
// ------------------------------------------------------------
#define FONT_SM     0   // 8×16px  – etykiety, statusy
#define FONT_MD     1   // 12×24px – wartości, menu
#define FONT_LG     2   // 16×32px – główne wartości (RPM, pozycja)

#define FONT_SM_W   8
#define FONT_SM_H   16
#define FONT_MD_W   12
#define FONT_MD_H   24
#define FONT_LG_W   16
#define FONT_LG_H   32

// ── Adaptacyjne rozmiary fontów – zależne od rozdzielczości ──
#if DISP_H >= 400      // 480×320, 800×480 itp.
  #define FONT_LABEL      FONT_MD
  #define FONT_LABEL_W    FONT_MD_W
  #define FONT_LABEL_H    FONT_MD_H
  #define FONT_VALUE      FONT_LG
  #define FONT_VALUE_W    FONT_LG_W
  #define FONT_VALUE_H    FONT_LG_H
  #define FONT_HEADER     FONT_LG
#elif DISP_H >= 240     // 320×240
  #define FONT_LABEL      FONT_SM
  #define FONT_LABEL_W    FONT_SM_W
  #define FONT_LABEL_H    FONT_SM_H
  #define FONT_VALUE      FONT_MD
  #define FONT_VALUE_W    FONT_MD_W
  #define FONT_VALUE_H    FONT_MD_H
  #define FONT_HEADER     FONT_LG
#else                   // 160×128, 128×128
  #define FONT_LABEL      FONT_SM
  #define FONT_LABEL_W    FONT_SM_W
  #define FONT_LABEL_H    FONT_SM_H
  #define FONT_VALUE      FONT_SM
  #define FONT_VALUE_W    FONT_SM_W
  #define FONT_VALUE_H    FONT_SM_H
  #define FONT_HEADER     FONT_MD
#endif

#define FONT_MN_SM  3   // Mincho 8×16px
#define FONT_MN_MD  4   // Mincho 12×24px
#define FONT_MN_LG  5   // Mincho 16×32px
#define FONT_LATIN  6   // Latin 32B

#define FONT_MN_SM_W  8
#define FONT_MN_SM_H  16
#define FONT_MN_MD_W  12
#define FONT_MN_MD_H  24
#define FONT_MN_LG_W  16
#define FONT_MN_LG_H  32

// ------------------------------------------------------------
//  Kolory RGB565 – makro biblioteki
// ------------------------------------------------------------
#define COL_BLACK       rgb565(  0,   0,   0)
#define COL_WHITE       rgb565(255, 255, 255)
#define COL_RED         rgb565(255,   0,   0)
#define COL_GREEN       rgb565(  0, 200,   0)
#define COL_BLUE        rgb565(  0,   0, 255)
#define COL_YELLOW      rgb565(255, 255,   0)
#define COL_CYAN        rgb565(  0, 255, 255)
#define COL_MAGENTA     rgb565(255,   0, 255)
#define COL_ORANGE      rgb565(255, 140,   0)
#define COL_DARK_GREY   rgb565( 64,  64,  64)
#define COL_LIGHT_GREY  rgb565(192, 192, 192)

// ------------------------------------------------------------
//  Globalne uchwyty
// ------------------------------------------------------------
extern TFT_t      g_dev;
extern FontxFile  g_font[7][2];

// ------------------------------------------------------------
//  API publiczne
// ------------------------------------------------------------
esp_err_t   display_init(void);
void        display_flush(void);
void        display_clear(uint16_t color);
void        display_pixel(int x, int y, uint16_t color);
void        display_hline(int x, int y, int w, uint16_t color);
void        display_vline(int x, int y, int h, uint16_t color);
void        display_fill_rect(int x, int y, int w, int h, uint16_t color);
void        display_draw_rect(int x, int y, int w, int h, uint16_t color);
void        display_string(int x, int y, const char *s,
                           uint8_t font_size, uint16_t fg, uint16_t bg);
void        display_int(int x, int y, int32_t val,
                        uint8_t font_size, uint16_t fg, uint16_t bg);
void        display_brightness(uint8_t pct);
int         display_text_width(const char *s, uint8_t font_size);

// Bitmap from raw RGB565 data or SPIFFS file
void display_draw_bitmap(int x, int y, int w, int h, const uint16_t *data);
void display_draw_bitmap_file(int x, int y, const char *spiffs_path);
