#pragma once
#include "axis.h"
#include <stdint.h>
#include <stdbool.h>

#define STEPPER_STEPS_PER_REV   AXIS_Z_STEPS_PER_REV
#define STEPPER_LEAD_MM         AXIS_Z_LEAD_MM
#define STEPPER_STEPS_PER_MM    AXIS_Z_STEPS_PER_MM
#define STEPPER_SPEED_MAX       AXIS_SPEED_MAX_Z
#define STEPPER_SPEED_START     AXIS_SPEED_START
#define STEPPER_PIN_STEP        AXIS_Z_PIN_STEP
#define STEPPER_PIN_DIR         AXIS_Z_PIN_DIR
#define STEPPER_PIN_ENA         AXIS_Z_PIN_ENA

typedef axis_state_t    stepper_state_t;
typedef axis_dir_t      stepper_dir_t;
typedef axis_move_t     stepper_move_t;

#define STEPPER_STATE_IDLE   AXIS_STATE_IDLE
#define STEPPER_STATE_RUN    AXIS_STATE_RUN
#define STEPPER_STATE_ACCEL  AXIS_STATE_ACCEL
#define STEPPER_STATE_DECEL  AXIS_STATE_DECEL
#define STEPPER_STATE_ERROR  AXIS_STATE_ERROR
#define STEPPER_DIR_CW       AXIS_DIR_POS
#define STEPPER_DIR_CCW      AXIS_DIR_NEG

static inline void stepper_init(void)                                         { axis_init(AXIS_Z); }
static inline void stepper_stop(void)                                         { axis_stop(g_axis_z); }
static inline void stepper_enable(bool en)                                    { axis_enable(g_axis_z, en); }
static inline void stepper_jog(stepper_dir_t d, uint16_t s, uint8_t p)       { axis_jog(g_axis_z, d, s, p); }
static inline void stepper_run(stepper_dir_t d, float sp)                     { axis_run(g_axis_z, d, sp); }
static inline bool stepper_move(stepper_move_t *mv)                           { return axis_move(g_axis_z, mv); }
static inline float stepper_get_position_mm(void)                             { return axis_get_position_mm(g_axis_z); }
static inline int32_t stepper_get_position_steps(void)                        { return axis_get_position_steps(g_axis_z); }
static inline stepper_state_t stepper_get_state(void)                         { return axis_get_state(g_axis_z); }
static inline uint32_t stepper_get_speed(void)                                { return axis_get_speed(g_axis_z); }
static inline void stepper_reset_position(void)                               { axis_reset_position(g_axis_z); }
void stepper_set_microstep(uint16_t s);
