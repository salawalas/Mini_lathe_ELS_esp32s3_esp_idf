#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// GPIO dla buzzera – można zmienić w zależności od HW
// Domyślnie IO48 (wolny pin, brak konfliktu z TFT/SD/krańcówkami)
// Aktywny buzzer: wystarczy GPIO HIGH/LOW
// Pasywny buzzer: LEDC PWM ~2-4 kHz
#ifndef BUZZER_PIN_GPIO
#define BUZZER_PIN_GPIO     48
#endif

// Długości sygnałów (ms)
#define BUZZER_BEEP_SHORT   50
#define BUZZER_BEEP_MED     150
#define BUZZER_BEEP_LONG    400

// Inicjalizacja PWM buzzera
void buzzer_init(void);

// Blokujący sygnał dźwiękowy – czas w ms
void buzzer_beep(uint32_t ms);

// Sygnały złożone (nieblokujące – uruchamiają task)
void buzzer_signal_ok(void);        // krótki pojedynczy
void buzzer_signal_warn(void);      // dwa krótkie
void buzzer_signal_error(void);     // długi ciągły 500ms
void buzzer_signal_estop(void);     // trzy krótkie + długi