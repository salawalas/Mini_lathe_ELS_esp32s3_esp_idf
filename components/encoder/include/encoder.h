#pragma once
// ============================================================
//  encoder.h  –  GPIO dla enkodera obrotowego i przycisków
//  ESP32-S3-WROOM-1 N16R8  /  DevKitC-1
//  Optymalizacja v6: piny skupione fizycznie na PCB
// ============================================================
//
//  ENKODER – lewa strona, piny kolejne: IO4 / IO5 / IO6
//  ┌────────────────────────────────┐
//  │  IO4  ENC CLK                 │
//  │  IO5  ENC DT                  │
//  │  IO6  ENC SW (przycisk)       │
//  └────────────────────────────────┘
#define ENCODER_PIN_CLK     4
#define ENCODER_PIN_DT      5
#define ENCODER_PIN_SW      6

//  PRZYCISKI – lewa strona, piny kolejne: IO7 / IO15 / IO16
//  ┌────────────────────────────────┐
//  │  IO7   BTN 1                  │
//  │  IO15  BTN 2                  │
//  │  IO16  BTN 3                  │
//  └────────────────────────────────┘
#define BUTTON_PIN_1        7
#define BUTTON_PIN_2        15
#define BUTTON_PIN_3        16

#include <stdint.h>
#include <stdbool.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Encoder tuning
#define ENCODER_MIN_DELTA    1    // ignore smaller PCNT changes
#define ENCODER_DIVIDER      4    // emit one event per N encoder pulses

// Button timing
#define BUTTON_DEBOUNCE_MS   20
#define BUTTON_LONG_PRESS_MS 800

typedef enum {
	ENCODER_EVT_CW = 0,
	ENCODER_EVT_CCW,
	ENCODER_EVT_SW_PRESS,
	ENCODER_EVT_SW_LONG,
	ENCODER_EVT_BTN1_PRESS,
	ENCODER_EVT_BTN1_LONG,
	ENCODER_EVT_BTN2_PRESS,
	ENCODER_EVT_BTN2_LONG,
	ENCODER_EVT_BTN3_PRESS,
	ENCODER_EVT_BTN3_LONG,
} encoder_event_t;

typedef struct {
	encoder_event_t type;
	int32_t         position;
} encoder_msg_t;

// Public API
void encoder_init(void);
bool encoder_get_event(encoder_msg_t *msg, TickType_t timeout);
int32_t encoder_get_position(void);
void encoder_reset_position(void);
void encoder_set_position(int32_t pos);
bool encoder_button_pressed(uint8_t btn_num);

