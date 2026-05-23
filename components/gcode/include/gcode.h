#pragma once
// ============================================================
//  gcode.h  –  Parser i executor G-code dla tokarki CNC
//  Mini Lathe Controller v4  /  ESP32
// ============================================================
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// ── Stan wykonywania ──
typedef enum {
    GCODE_STATE_IDLE = 0,
    GCODE_STATE_RUNNING,
    GCODE_STATE_PAUSED,
    GCODE_STATE_ERROR,
    GCODE_STATE_DONE,
} gcode_state_t;

// ── Typy ruchu ──
typedef enum {
    GCODE_MOVE_RAPID = 0,   // G0
    GCODE_MOVE_LINEAR,      // G1
} gcode_move_t;

// ── Jednostki ──
typedef enum {
    GCODE_UNITS_MM = 0,
    GCODE_UNITS_INCH,
} gcode_units_t;

// ── Tryb odległości ──
typedef enum {
    GCODE_DISTANCE_ABSOLUTE = 0,   // G90
    GCODE_DISTANCE_RELATIVE,       // G91
} gcode_distance_t;

// ── Plaszczyzna (na tokarkę głównie Z/X) ──
typedef enum {
    GCODE_PLANE_ZX = 0,
} gcode_plane_t;

// ── Status wykonania ──
typedef struct {
    gcode_state_t   state;
    uint32_t        line_current;     // bieżąca linia (1-based)
    uint32_t        line_total;       // całkowita liczba linii w pliku
    float           pos_z_mm;         // aktualna pozycja Z wg parsera
    float           pos_x_mm;         // aktualna pozycja X wg parsera
    uint16_t        spindle_rpm;      // aktualne RPM
    bool            spindle_on;
    char            error_msg[64];    // ostatni błąd
    char            file_name[64];    // nazwa pliku
} gcode_status_t;

// ── Inicjalizacja ──
void gcode_init(void);

// ── Ładowanie pliku z SD ──
// Zwraca liczbę linii (0 = błąd)
uint32_t gcode_load_file(const char *filename);

// ── Start / Stop / Pauza ──
bool gcode_start(void);
void gcode_stop(void);
void gcode_pause(void);
void gcode_resume(void);

// ── Status ──
void gcode_get_status(gcode_status_t *st);
gcode_state_t gcode_get_state(void);
bool gcode_is_running(void);

// ── Wykonaj jedną linię G-code bezpośrednio (dla MDI) ──
bool gcode_exec_line(const char *line);