#pragma once
// ============================================================
//  display_compat.h  –  Kompatybilność ui_menu.c → display v3
//  Mini Lathe Controller v3
//
//  Mapowanie scale → font_size:
//    scale=1 → FONT_SM (8×16px)
//    scale=2 → FONT_MD (12×24px)
//    scale=3 → FONT_LG (16×32px)
// ============================================================

#include "display.h"
#include "ili9340.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Kolory – aliasy
#define COLOR_BLACK      COL_BLACK
#define COLOR_WHITE      COL_WHITE
#define COLOR_RED        COL_RED
#define COLOR_GREEN      COL_GREEN
#define COLOR_BLUE       COL_BLUE
#define COLOR_YELLOW     COL_YELLOW
#define COLOR_CYAN       COL_CYAN
#define COLOR_MAGENTA    COL_MAGENTA
#define COLOR_ORANGE     COL_ORANGE
#define COLOR_DARK_GREY  COL_DARK_GREY
#define COLOR_LIGHT_GREY COL_LIGHT_GREY
#define RGB565(r,g,b)    rgb565(r,g,b)

#define DISPLAY_WIDTH    DISP_W
#define DISPLAY_HEIGHT   DISP_H

static inline uint8_t _scale_to_font(uint8_t scale)
{
    if (scale >= 3) return FONT_LG;
    if (scale == 2) return FONT_MD;
    return FONT_SM;
}

static inline void display_fill(uint16_t c)
    { display_clear(c); }

static inline void display_draw_string(int16_t x, int16_t y,
    const char *s, uint16_t fg, uint16_t bg, uint8_t scale)
    { display_string(x, y, s, _scale_to_font(scale), fg, bg); }

static inline void display_draw_char(int16_t x, int16_t y,
    char c, uint16_t fg, uint16_t bg, uint8_t scale)
    { char b[2]={c,0}; display_string(x, y, b, _scale_to_font(scale), fg, bg); }

static inline void display_draw_int(int16_t x, int16_t y,
    int32_t val, uint16_t fg, uint16_t bg, uint8_t scale)
    { char b[16]; snprintf(b,sizeof(b),"%ld",(long)val);
      display_string(x, y, b, _scale_to_font(scale), fg, bg); }

static inline void display_draw_hline(int16_t x, int16_t y,
    int16_t w, uint16_t c)
    { display_hline(x, y, w, c); }

static inline void display_draw_pixel(int16_t x, int16_t y, uint16_t c)
    { display_pixel(x, y, c); }

static inline void display_set_backlight(uint8_t pct)
    { display_brightness(pct); }
