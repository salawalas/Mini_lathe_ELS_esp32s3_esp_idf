#pragma once
#include <stdint.h>
#include <stdbool.h>

// Pin definitions (board-specific; update if needed)
#define AXIS_Z_PIN_STEP     41
#define AXIS_Z_PIN_DIR      40
#define AXIS_Z_PIN_ENA      39

#define AXIS_X_PIN_STEP     1
#define AXIS_X_PIN_DIR      2
#define AXIS_X_PIN_ENA      42

// Enable logic
#define AXIS_ENA_ACTIVE     1
#define AXIS_ENA_IDLE       0

// Mechanical parameters (defaults)
#define AXIS_Z_STEPS_PER_REV    12800
#define AXIS_Z_LEAD_MM          2.0f
#define AXIS_Z_STEPS_PER_MM     ((float)AXIS_Z_STEPS_PER_REV / AXIS_Z_LEAD_MM)

#define AXIS_X_STEPS_PER_REV    12800
#define AXIS_X_LEAD_MM          1.25f
#define AXIS_X_STEPS_PER_MM     ((float)AXIS_X_STEPS_PER_REV / AXIS_X_LEAD_MM)

// Motion parameters
#define AXIS_SPEED_START        1000
#define AXIS_SPEED_MAX_Z        44800
#define AXIS_SPEED_MAX_X        20000
#define AXIS_ACCEL              20000

// Timer / alarm limits
#define AXIS_TIMER_RES_HZ       1000000UL
#define AXIS_ALARM_MIN_US       10UL
#define AXIS_ALARM_MAX_US       (AXIS_TIMER_RES_HZ / AXIS_SPEED_START)

typedef enum { AXIS_Z = 0, AXIS_X = 1, AXIS_COUNT } axis_id_t;
typedef enum { AXIS_STATE_IDLE = 0, AXIS_STATE_RUN, AXIS_STATE_ACCEL, AXIS_STATE_DECEL, AXIS_STATE_ERROR } axis_state_t;
typedef enum { AXIS_DIR_POS = 0, AXIS_DIR_NEG = 1 } axis_dir_t;

typedef struct { int32_t steps; float speed_mm_min; } axis_move_t;

typedef struct {
	axis_state_t state;
	axis_dir_t   dir;
	int32_t      position_steps;
	float        position_mm;
	uint32_t     speed_steps_s;
	float        speed_mm_min;
} axis_status_t;

typedef struct axis_handle_s *axis_handle_t;

// Global axis handles (initialized by axis_init)
extern axis_handle_t g_axis_z;
extern axis_handle_t g_axis_x;

// API
axis_handle_t axis_init(axis_id_t id);
void axis_stop(axis_handle_t ax);
void axis_enable(axis_handle_t ax, bool en);
void axis_jog(axis_handle_t ax, axis_dir_t dir, uint16_t steps, uint8_t pct);
void axis_run(axis_handle_t ax, axis_dir_t dir, float speed_mm_min);
bool axis_move(axis_handle_t ax, axis_move_t *mv);
bool axis_move_to_mm(axis_handle_t ax, float target_mm, float speed_mm_min);
void axis_get_status(axis_handle_t ax, axis_status_t *st);
float  axis_get_position_mm(axis_handle_t ax);
int32_t axis_get_position_steps(axis_handle_t ax);
axis_state_t axis_get_state(axis_handle_t ax);
uint32_t axis_get_speed(axis_handle_t ax);
void axis_reset_position(axis_handle_t ax);
void axis_set_position(axis_handle_t ax, float pos_mm);
void axis_set_lead_mm(axis_handle_t ax, float lead_mm);
void axis_set_max_speed_steps_s(axis_handle_t ax, uint32_t sps);
float axis_get_steps_per_mm(axis_handle_t ax);

