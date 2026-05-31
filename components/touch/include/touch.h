#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

// Typ kontrolera dotyku
typedef enum {
    TOUCH_NONE = 0,
    TOUCH_XPT2046,      // SPI, rezystancyjny (najczęstszy)
    TOUCH_FT6X06,       // I2C, pojemnościowy (FT6206/FT6336)
    TOUCH_GT911,        // I2C, pojemnościowy (Goodix)
} touch_type_t;

// Zdarzenie dotyku
typedef enum {
    TOUCH_EVT_NONE = 0,
    TOUCH_EVT_PRESS,
    TOUCH_EVT_RELEASE,
    TOUCH_EVT_HOLD,         // przytrzymanie > 500ms
    TOUCH_EVT_SWIPE_LEFT,
    TOUCH_EVT_SWIPE_RIGHT,
    TOUCH_EVT_SWIPE_UP,
    TOUCH_EVT_SWIPE_DOWN,
} touch_event_t;

// Surowe współrzędne (sprzed kalibracji)
typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t z;         // nacisk (0 = brak dotyku, >0 dla XPT2046)
} touch_raw_t;

// Skalibrowane współrzędne (0..width, 0..height)
typedef struct {
    uint16_t x;
    uint16_t y;
    bool     pressed;
} touch_point_t;

// Kalibracja 3-punktowa
typedef struct {
    // Surowe odczyty z 3 punktów kalibracyjnych
    touch_raw_t raw[3];
    // Oczekiwane współrzędne ekranowe dla tych punktów
    uint16_t    ref_x[3];
    uint16_t    ref_y[3];
    // Wynikowe współczynniki
    float scale_x, offset_x;
    float scale_y, offset_y;
} touch_calib_t;

// ── API ────────────────────────────────────────────────────

// Inicjalizacja kontrolera dotyku (typ i piny z menuconfig)
esp_err_t touch_init(void);

// Odczyt surowych współrzędnych (bez kalibracji)
esp_err_t touch_read_raw(touch_raw_t *out);

// Odczyt skalibrowanych współrzędnych (0..screen_w, 0..screen_h)
esp_err_t touch_read(touch_point_t *out);

// Sprawdź czy ekran jest dotknięty
bool touch_is_pressed(void);

// Kalibracja
void touch_calib_reset(touch_calib_t *cal);
void touch_calib_add_point(touch_calib_t *cal, const touch_raw_t *raw,
                           uint16_t ref_x, uint16_t ref_y);
bool touch_calib_compute(touch_calib_t *cal);
void touch_calib_apply(const touch_calib_t *cal);
void touch_calib_save_to_nvs(const touch_calib_t *cal);
bool touch_calib_load_from_nvs(touch_calib_t *cal);

// Wykrywanie gestów (wywołuj okresowo z raw.x, raw.y, raw.z)
touch_event_t touch_detect_gesture(const touch_raw_t *raw, uint32_t now_ms);
