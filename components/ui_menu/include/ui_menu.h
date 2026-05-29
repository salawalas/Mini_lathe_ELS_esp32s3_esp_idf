#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    SCREEN_MAIN=0, SCREEN_MENU, SCREEN_JOG, SCREEN_FEED,
    SCREEN_SPINDLE, SCREEN_SETTINGS, SCREEN_ELS, SCREEN_AXIS_X,
    SCREEN_HOMING,      // Bazowanie osi
    SCREEN_BACKLIGHT,   // Podsiwetlenie
    SCREEN_GCODE,       // G-code z karty SD
    SCREEN_POSITION,    // Pozycja / presety osi
    SCREEN_DRO,         // DRO – duże cyfry Z/X
    SCREEN_COUNT
} screen_id_t;

void        ui_menu_init(void);
void        ui_menu_goto(screen_id_t screen);
screen_id_t ui_menu_current_screen(void);
void        ui_menu_notify(const char *msg, uint16_t color, uint32_t ms);

// Globalny stan bazowania – dostępny z innych modułów
extern volatile bool g_homed;
