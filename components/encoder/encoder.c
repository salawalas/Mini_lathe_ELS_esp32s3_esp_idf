// encoder.c – enkoder PCNT + przyciski GPIO ISR
#include "encoder.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/pulse_cnt.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "ENCODER";

static QueueHandle_t    s_event_queue = NULL;
static QueueHandle_t    s_gpio_queue  = NULL;
static pcnt_unit_handle_t  s_pcnt_unit  = NULL;
static pcnt_channel_handle_t s_pcnt_ch_a = NULL;
static pcnt_channel_handle_t s_pcnt_ch_b = NULL;

static volatile int32_t s_position   = 0;
static volatile int16_t s_last_pcnt  = 0;

typedef struct {
    int         gpio;
    uint8_t     btn_id;
    encoder_event_t evt_short;
    encoder_event_t evt_long;
    volatile bool   raw_state;
    volatile int64_t press_time_us;
    volatile bool   handled;
} button_t;

static button_t s_buttons[] = {
    { ENCODER_PIN_SW, 0, ENCODER_EVT_SW_PRESS,   ENCODER_EVT_SW_LONG,   false, 0, true },
    { BUTTON_PIN_1,   1, ENCODER_EVT_BTN1_PRESS,  ENCODER_EVT_BTN1_LONG, false, 0, true },
    { BUTTON_PIN_2,   2, ENCODER_EVT_BTN2_PRESS,  ENCODER_EVT_BTN2_LONG, false, 0, true },
    { BUTTON_PIN_3,   3, ENCODER_EVT_BTN3_PRESS,  ENCODER_EVT_BTN3_LONG, false, 0, true },
};
#define BTN_COUNT (sizeof(s_buttons)/sizeof(s_buttons[0]))

static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    uint32_t btn_id = (uint32_t)(uintptr_t)arg;
    BaseType_t hp = pdFALSE;
    xQueueSendFromISR(s_gpio_queue, &btn_id, &hp);
    if (hp) portYIELD_FROM_ISR();
}

static void button_task(void *arg)
{
    uint32_t btn_id;
    while (1) {
        if (xQueueReceive(s_gpio_queue, &btn_id, portMAX_DELAY) != pdTRUE) continue;
        if (btn_id >= BTN_COUNT) continue;
        button_t *btn = &s_buttons[btn_id];
        vTaskDelay(pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS));
        bool current = (gpio_get_level(btn->gpio) == 0);
        if (current && btn->handled) {
            btn->raw_state     = true;
            btn->press_time_us = esp_timer_get_time();
            btn->handled       = false;
        } else if (!current && !btn->handled) {
            int64_t ms = (esp_timer_get_time() - btn->press_time_us) / 1000;
            encoder_event_t evt = (ms >= BUTTON_LONG_PRESS_MS) ? btn->evt_long : btn->evt_short;
            btn->raw_state = false;
            btn->handled   = true;
            encoder_msg_t msg = { .type = evt, .position = s_position };
            xQueueSend(s_event_queue, &msg, 0);
        }
    }
}

static void encoder_task(void *arg)
{
    // Akumulator dla dzielnika – zbiera impulsy PCNT
    static int16_t s_accum = 0;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5));
        int count = 0;
        pcnt_unit_get_count(s_pcnt_unit, &count);
        int16_t delta = (int16_t)count - s_last_pcnt;

        // Filtruj drgania mechaniczne – ignoruj za małe zmiany
        if (delta > -ENCODER_MIN_DELTA && delta < ENCODER_MIN_DELTA) continue;

        s_last_pcnt = (int16_t)count;
        s_position += delta;

        // Akumuluj impulsy do dzielnika
        s_accum += delta;

        // Generuj zdarzenie co ENCODER_DIVIDER impulsów
        int events = s_accum / ENCODER_DIVIDER;
        s_accum   %= ENCODER_DIVIDER;

        if (events == 0) continue;

        encoder_event_t evt = (events > 0) ? ENCODER_EVT_CW : ENCODER_EVT_CCW;
        int abs_events = (events > 0) ? events : -events;

        for (int i = 0; i < abs_events; i++) {
            encoder_msg_t msg = { .type = evt, .position = s_position };
            xQueueSend(s_event_queue, &msg, 0);
        }
    }
}

void encoder_init(void)
{
    s_event_queue = xQueueCreate(16, sizeof(encoder_msg_t));
    s_gpio_queue  = xQueueCreate(8,  sizeof(uint32_t));

    // PCNT
    pcnt_unit_config_t ucfg = { .high_limit=32000, .low_limit=-32000,
                                  .flags.accum_count=true };
    pcnt_new_unit(&ucfg, &s_pcnt_unit);
    pcnt_glitch_filter_config_t flt = { .max_glitch_ns=1000 };
    pcnt_unit_set_glitch_filter(s_pcnt_unit, &flt);

    pcnt_chan_config_t ca = { .edge_gpio_num=ENCODER_PIN_CLK, .level_gpio_num=ENCODER_PIN_DT };
    pcnt_new_channel(s_pcnt_unit, &ca, &s_pcnt_ch_a);
    pcnt_chan_config_t cb = { .edge_gpio_num=ENCODER_PIN_DT, .level_gpio_num=ENCODER_PIN_CLK };
    pcnt_new_channel(s_pcnt_unit, &cb, &s_pcnt_ch_b);

    pcnt_channel_set_edge_action(s_pcnt_ch_a, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE);
    pcnt_channel_set_level_action(s_pcnt_ch_a, PCNT_CHANNEL_LEVEL_ACTION_KEEP,   PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
    pcnt_channel_set_edge_action(s_pcnt_ch_b, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE);
    pcnt_channel_set_level_action(s_pcnt_ch_b, PCNT_CHANNEL_LEVEL_ACTION_KEEP,   PCNT_CHANNEL_LEVEL_ACTION_INVERSE);

    pcnt_unit_enable(s_pcnt_unit);
    pcnt_unit_clear_count(s_pcnt_unit);
    pcnt_unit_start(s_pcnt_unit);

    // GPIO przyciski
    gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    for (int i = 0; i < (int)BTN_COUNT; i++) {
        gpio_config_t io = {
            .pin_bit_mask = (1ULL << s_buttons[i].gpio),
            .mode         = GPIO_MODE_INPUT,
            .pull_up_en   = GPIO_PULLUP_ENABLE,   // S3: wszystkie GPIO mają wewn. pull-up
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_ANYEDGE,
        };
        gpio_config(&io);
        gpio_isr_handler_add(s_buttons[i].gpio, gpio_isr_handler, (void*)(uintptr_t)i);
    }

    xTaskCreate(button_task,  "btn_task", 2048, NULL, 5, NULL);
    xTaskCreate(encoder_task, "enc_task", 2048, NULL, 5, NULL);
    ESP_LOGI(TAG, "Enkoder gotowy");
}

bool encoder_get_event(encoder_msg_t *msg, TickType_t timeout)
{
    return xQueueReceive(s_event_queue, msg, timeout) == pdTRUE;
}

int32_t encoder_get_position(void) { return s_position; }

void encoder_reset_position(void)
{
    s_position  = 0;
    s_last_pcnt = 0;
    pcnt_unit_clear_count(s_pcnt_unit);
}

void encoder_set_position(int32_t pos)
{
    s_position  = pos;
    s_last_pcnt = 0;
    pcnt_unit_clear_count(s_pcnt_unit);
}

bool encoder_button_pressed(uint8_t btn_num)
{
    if (btn_num >= BTN_COUNT) return false;
    return s_buttons[btn_num].raw_state;
}
