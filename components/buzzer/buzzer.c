// buzzer.c – Sygnalizator dźwiękowy (GPIO PWM)

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "buzzer.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"

static const char *TAG = "BUZZER";

#define BUZZER_LEDC_TIMER   LEDC_TIMER_1
#define BUZZER_LEDC_CHANNEL LEDC_CHANNEL_1
#define BUZZER_FREQ_HZ      4000

void buzzer_init(void)
{
    ledc_timer_config_t t = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = BUZZER_LEDC_TIMER,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .freq_hz         = BUZZER_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&t);

    ledc_channel_config_t ch = {
        .gpio_num   = BUZZER_PIN_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = BUZZER_LEDC_CHANNEL,
        .timer_sel  = BUZZER_LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    ledc_channel_config(&ch);
    ledc_fade_func_install(0);

    gpio_set_level(BUZZER_PIN_GPIO, 0);
    ESP_LOGI(TAG, "Buzzer ready (GPIO%d, %dHz)", BUZZER_PIN_GPIO, BUZZER_FREQ_HZ);
}

void buzzer_beep(uint32_t ms)
{
    if (ms == 0) return;

    // Start PWM (50% duty)
    ledc_set_fade_with_time(LEDC_LOW_SPEED_MODE,
                            BUZZER_LEDC_CHANNEL, 128, ms);
    ledc_fade_start(LEDC_LOW_SPEED_MODE,
                    BUZZER_LEDC_CHANNEL, LEDC_FADE_NO_WAIT);

    // Czekaj i wycisz
    vTaskDelay(pdMS_TO_TICKS(ms));
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BUZZER_LEDC_CHANNEL, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BUZZER_LEDC_CHANNEL);
}

// ── Task dla sygnałów złożonych (nieblokujące) ──
static void buzzer_pattern_task(void *arg)
{
    uint32_t pattern = (uint32_t)(uintptr_t)arg;

    switch (pattern) {
    case 0: // OK – krótki pojedynczy
        buzzer_beep(BUZZER_BEEP_SHORT);
        break;
    case 1: // WARN – dwa krótkie
        buzzer_beep(BUZZER_BEEP_SHORT);
        vTaskDelay(pdMS_TO_TICKS(80));
        buzzer_beep(BUZZER_BEEP_SHORT);
        break;
    case 2: // ERROR – długi
        buzzer_beep(BUZZER_BEEP_LONG);
        break;
    case 3: // E-STOP – trzy krótkie + długi
        for (int i = 0; i < 3; i++) {
            buzzer_beep(BUZZER_BEEP_SHORT);
            vTaskDelay(pdMS_TO_TICKS(60));
        }
        vTaskDelay(pdMS_TO_TICKS(100));
        buzzer_beep(BUZZER_BEEP_LONG);
        break;
    }
    vTaskDelete(NULL);
}

void buzzer_signal_ok(void)
{
    xTaskCreate(buzzer_pattern_task, "buz_ok", 1536,
                (void*)(uintptr_t)0, 3, NULL);
}

void buzzer_signal_warn(void)
{
    xTaskCreate(buzzer_pattern_task, "buz_warn", 1536,
                (void*)(uintptr_t)1, 3, NULL);
}

void buzzer_signal_error(void)
{
    xTaskCreate(buzzer_pattern_task, "buz_err", 1536,
                (void*)(uintptr_t)2, 3, NULL);
}

void buzzer_signal_estop(void)
{
    xTaskCreate(buzzer_pattern_task, "buz_estop", 1536,
                (void*)(uintptr_t)3, 3, NULL);
}