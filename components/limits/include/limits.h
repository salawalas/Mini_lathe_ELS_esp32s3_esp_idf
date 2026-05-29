#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "axis.h"

// ============================================================
//  limits.h  – limit switches GPIO and API
// ============================================================

// GPIO pins for limit switches (board-specific)
#define LIMIT_Z_MIN_GPIO    8
#define LIMIT_Z_MAX_GPIO    3     // INPUT-ONLY – external pull-up required
#define LIMIT_X_MIN_GPIO    46
#define LIMIT_X_MAX_GPIO    45

// Limit identifiers
typedef enum {
	LIMIT_Z_MIN = 0,
	LIMIT_Z_MAX,
	LIMIT_X_MIN,
	LIMIT_X_MAX,
	LIMIT_COUNT
} limit_id_t;

typedef struct {
	limit_id_t id;
	bool       triggered;
	const char *name;
} limit_status_t;

// ── Software limits (fallback when HW limit switches are disconnected) ──
// Active only after homing (s_homed[axis] == true).
// Range: 0 mm = home position; negative travel allowed only for Z (carriage toward headstock).
#define SOFT_LIMIT_Z_MIN_MM   (-250.0f)
#define SOFT_LIMIT_Z_MAX_MM   (  10.0f)
#define SOFT_LIMIT_X_MIN_MM   ( -80.0f)
#define SOFT_LIMIT_X_MAX_MM   (  10.0f)

// API
void    limits_init(void);
bool    limits_is_triggered(limit_id_t id);
void    limits_get_status(limit_id_t id, limit_status_t *st);
bool    limits_any_triggered(void);

// Check if moving `ax` in `dir` is allowed (not blocked by limits/homing)
bool    limits_can_move(axis_handle_t ax, axis_dir_t dir);

// Homing: approach specified limit switch, back off and set position 0
bool    limits_home_axis(axis_handle_t ax, limit_id_t home_switch,
						 axis_dir_t approach_dir, float speed_mm_min);

// Callback registration (called from ISR)
void    limits_register_callback(void (*cb)(limit_id_t, void*), void *arg);

// Clear triggered flag (after manual move)
void    limits_clear(limit_id_t id);

// Track axis homed state
bool    limits_axis_homed(axis_id_t axis_id);
void    limits_set_axis_homed(axis_id_t axis_id, bool homed);
bool    limits_all_homed(void);
