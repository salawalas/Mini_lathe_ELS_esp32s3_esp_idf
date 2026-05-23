#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "stepper.h"
#include "spindle.h"

typedef enum { THREAD_METRIC=0, THREAD_IMPERIAL=1 } thread_type_t;
typedef struct { const char *label; thread_type_t type; float pitch; } thread_preset_t;
typedef enum { ELS_STATE_IDLE=0, ELS_STATE_WAITING, ELS_STATE_RUNNING, ELS_STATE_RETRACTING, ELS_STATE_ERROR } els_state_t;
typedef struct { float pitch_mm; stepper_dir_t feed_dir; float z_start_mm, z_end_mm; uint8_t passes; float depth_per_pass; } els_config_t;
typedef struct { els_state_t state; float pitch_mm; uint8_t pass_current, pass_total; float z_pos_mm; uint16_t spindle_rpm; int32_t steps_sent; bool sync_ok; } els_status_t;

extern const thread_preset_t ELS_THREAD_PRESETS[];
extern const uint8_t         ELS_THREAD_PRESETS_COUNT;

void  els_init(void);
bool  els_start(const els_config_t *cfg);
void  els_stop(void);
void  els_get_status(els_status_t *status);
bool  els_is_running(void);
float els_tpi_to_mm(float tpi);
float els_feed_steps_per_spindle_rev(float pitch_mm);
float els_feed_steps_per_spindle_rev_runtime(float pitch_mm);
