#pragma once
// ============================================================
//  spindle.h  –  GPIO dla wrzeciona
//  ESP32-S3-WROOM-1 N16R8  /  DevKitC-1
//  Optymalizacja v6: piny skupione fizycznie na PCB
// ============================================================
//
//  WRZECIONO STEP/DIR/ENA – prawa strona, piny kolejne: IO47 / IO21 / IO20
//  ┌──────────────────────────────────────────────────┐
//  │  IO47  SP STEP                                   │
//  │  IO21  SP DIR                                    │
//  │  IO20  SP ENA   ⚠ = USB_D+                      │
//  └──────────────────────────────────────────────────┘
//  ⚠ IO20 to USB_D+. Używać tylko gdy USB JTAG/Serial nieaktywne.
//    Jeśli debugger USB jest potrzebny, przenieś ENA np. na IO43 (TX).
//
//  WRZECIONO AUX – lewa strona, piny kolejne: IO17 / IO18
//  ┌──────────────────────────────────────────────────┐
//  │  IO17  SP POWER   (włącznik zasilania wrzeciona) │
//  │  IO18  SP ESTOP   (emergency stop)               │
//  └──────────────────────────────────────────────────┘

#define SPINDLE_PIN_STEP    47
#define SPINDLE_PIN_DIR     21
#define SPINDLE_PIN_ENA     20    // ⚠ USB_D+ – patrz uwaga wyżej
#define SPINDLE_ENA_ACTIVE  1
#define SPINDLE_ENA_IDLE    0
#define SPINDLE_POWER_PIN   17
#define SPINDLE_ESTOP_PIN   18

#define SPINDLE_STEPS_PER_REV   12800
#define SPINDLE_GEAR_RATIO      6.0f
#define SPINDLE_STEPS_PER_WREV  ((uint32_t)((float)SPINDLE_STEPS_PER_REV * SPINDLE_GEAR_RATIO))
#define SPINDLE_RPM_MAX     120
#define SPINDLE_RPM_MIN     10
#define SPINDLE_RAMP_MS     3000
#define SPINDLE_SPEED_START 500
#define SPINDLE_ACCEL       15000

#define SPINDLE_RPM_TO_SPS(rpm) ((uint32_t)((float)(rpm) * (float)SPINDLE_STEPS_PER_WREV / 60.0f))
#define SPINDLE_SPS_TO_RPM(sps) ((uint16_t)((float)(sps) * 60.0f / (float)SPINDLE_STEPS_PER_WREV))

#include <stdint.h>
#include <stdbool.h>

typedef enum {
	SPINDLE_STATE_ESTOP = 0,
	SPINDLE_STATE_STOPPED,
	SPINDLE_STATE_RAMPING_UP,
	SPINDLE_STATE_RAMPING_DOWN,
	SPINDLE_STATE_RUNNING,
} spindle_state_t;

typedef enum { SPINDLE_DIR_FWD = 0, SPINDLE_DIR_REV = 1 } spindle_dir_t;

typedef struct {
	spindle_state_t state;
	spindle_dir_t   dir;
	uint16_t        rpm_target;
	uint32_t        steps_per_s;
	uint16_t        rpm_actual;
	int32_t         position;
	bool            power_enabled;
	bool            estop_active;
	bool            at_speed;
} spindle_status_t;

// Public API
void spindle_init(void);
void spindle_power_enable(bool en);
void spindle_start(uint16_t rpm, spindle_dir_t dir);
void spindle_stop(void);
void spindle_emergency_stop(void);
void spindle_set_rpm(uint16_t rpm);
void spindle_get_status(spindle_status_t *st);
uint16_t spindle_get_rpm(void);
bool spindle_is_at_speed(void);
int32_t spindle_get_position(void);
void spindle_reset_position(void);
bool spindle_power_is_enabled(void);
bool spindle_estop_is_active(void);
void spindle_estop_reset(void);
void spindle_register_step_callback(uint32_t n, void (*cb)(void*), void *arg);
void spindle_register_estop_callback(void (*cb)(void*), void *arg);
void spindle_set_max_rpm(uint16_t rpm);
