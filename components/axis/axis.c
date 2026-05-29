// axis.c – ogólny moduł osi liniowej Z i X
#include "axis.h"
#include "../limits/include/limits.h"
#include <math.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gptimer.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_rom_sys.h"

static const char *TAG = "AXIS";

typedef struct
{
    axis_id_t id;
    const char *name;
    int pin_step, pin_dir, pin_ena;
    float steps_per_mm;
    uint32_t speed_max;
} axis_config_t;

static axis_config_t AXIS_CFG[AXIS_COUNT] = {
    [AXIS_Z] = {AXIS_Z, "Z", AXIS_Z_PIN_STEP, AXIS_Z_PIN_DIR, AXIS_Z_PIN_ENA,
                AXIS_Z_STEPS_PER_MM, AXIS_SPEED_MAX_Z},
    [AXIS_X] = {AXIS_X, "X", AXIS_X_PIN_STEP, AXIS_X_PIN_DIR, AXIS_X_PIN_ENA,
                AXIS_X_STEPS_PER_MM, AXIS_SPEED_MAX_X},
};

typedef struct axis_handle_s
{
    axis_config_t *cfg;
    gptimer_handle_t timer;
    SemaphoreHandle_t move_done;
    volatile axis_state_t state;
    volatile axis_dir_t dir;
    volatile int32_t position, target, steps_to_stop;
    volatile uint32_t speed_current, speed_target;
    volatile bool step_high, stop_request, continuous;
} axis_t;

axis_handle_t g_axis_z = NULL;
axis_handle_t g_axis_x = NULL;
static axis_t s_axes[AXIS_COUNT];

static inline uint64_t IRAM_ATTR speed_to_alarm(uint32_t sps)
{
    if (!sps)
        return AXIS_ALARM_MAX_US;
    uint64_t p = AXIS_TIMER_RES_HZ / sps;
    if (p < AXIS_ALARM_MIN_US)
        p = AXIS_ALARM_MIN_US;
    if (p > AXIS_ALARM_MAX_US)
        p = AXIS_ALARM_MAX_US;
    return p;
}

static inline int32_t IRAM_ATTR decel_steps(uint32_t s)
{
    if (s <= AXIS_SPEED_START)
        return 0;
    int32_t dv = (int32_t)s - AXIS_SPEED_START;
    return dv * dv / (2 * AXIS_ACCEL) + 1;
}

static bool IRAM_ATTR axis_isr(gptimer_handle_t timer,
                               const gptimer_alarm_event_data_t *e, void *ctx)
{
    axis_t *ax = (axis_t *)ctx;
    BaseType_t hp = pdFALSE;

    if (ax->stop_request)
    {
        gpio_set_level(ax->cfg->pin_step, 0);
        ax->step_high = false;
        ax->state = AXIS_STATE_IDLE;
        ax->speed_current = 0;
        ax->stop_request = false;
        gptimer_stop(timer);
        xSemaphoreGiveFromISR(ax->move_done, &hp);
        return hp;
    }

    ax->step_high = !ax->step_high;
    gpio_set_level(ax->cfg->pin_step, ax->step_high ? 1 : 0);

    if (ax->step_high)
    {
        ax->position += (ax->dir == AXIS_DIR_POS) ? 1 : -1;
        if (!ax->continuous)
        {
            int32_t rem = (ax->dir == AXIS_DIR_POS)
                              ? ax->target - ax->position
                              : ax->position - ax->target;
            if (rem <= 0)
            {
                gpio_set_level(ax->cfg->pin_step, 0);
                ax->step_high = false;
                ax->state = AXIS_STATE_IDLE;
                ax->speed_current = 0;
                gptimer_stop(timer);
                xSemaphoreGiveFromISR(ax->move_done, &hp);
                return hp;
            }
            ax->steps_to_stop = decel_steps(ax->speed_current);
            if (rem <= ax->steps_to_stop)
            {
                ax->state = AXIS_STATE_DECEL;
                ax->speed_target = AXIS_SPEED_START;
            }
        }
        if (ax->speed_current < ax->speed_target)
        {
            ax->state = AXIS_STATE_ACCEL;
            uint32_t d = AXIS_ACCEL / ax->speed_current + 1;
            ax->speed_current += d;
            if (ax->speed_current > ax->speed_target)
                ax->speed_current = ax->speed_target;
        }
        else if (ax->speed_current > ax->speed_target)
        {
            ax->state = AXIS_STATE_DECEL;
            uint32_t d = AXIS_ACCEL / ax->speed_current + 1;
            if (d >= ax->speed_current - AXIS_SPEED_START)
                ax->speed_current = AXIS_SPEED_START;
            else
                ax->speed_current -= d;
            if (ax->continuous && ax->speed_current <= AXIS_SPEED_START && ax->stop_request)
            {
                gpio_set_level(ax->cfg->pin_step, 0);
                ax->step_high = false;
                ax->state = AXIS_STATE_IDLE;
                ax->speed_current = 0;
                gptimer_stop(timer);
                xSemaphoreGiveFromISR(ax->move_done, &hp);
                return hp;
            }
        }
        else
        {
            ax->state = AXIS_STATE_RUN;
        }
    }

    uint64_t half = speed_to_alarm(ax->speed_current) / 2;
    if (half < AXIS_ALARM_MIN_US)
        half = AXIS_ALARM_MIN_US;
    gptimer_alarm_config_t alarm = {.alarm_count = e->alarm_value + half, .flags.auto_reload_on_alarm = false};
    gptimer_set_alarm_action(timer, &alarm);
    return hp;
}

static void _start(axis_t *ax, axis_dir_t dir, uint32_t spt, int32_t steps, bool cont)
{
    ax->dir = dir;
    gpio_set_level(ax->cfg->pin_dir, (dir == AXIS_DIR_POS) ? 1 : 0);
    esp_rom_delay_us(10);
    ax->speed_target = (spt > ax->cfg->speed_max) ? ax->cfg->speed_max : spt;
    ax->speed_current = AXIS_SPEED_START;
    ax->continuous = cont;
    ax->stop_request = false;
    ax->step_high = false;
    ax->state = AXIS_STATE_ACCEL;
    if (!cont)
        ax->target = (dir == AXIS_DIR_POS) ? ax->position + steps : ax->position - steps;
    xSemaphoreTake(ax->move_done, 0);
    uint64_t half = speed_to_alarm(AXIS_SPEED_START) / 2;
    gptimer_alarm_config_t alarm = {.alarm_count = half, .flags.auto_reload_on_alarm = false};
    ESP_ERROR_CHECK(gptimer_set_raw_count(ax->timer, 0));
    ESP_ERROR_CHECK(gptimer_set_alarm_action(ax->timer, &alarm));
    ESP_ERROR_CHECK(gptimer_start(ax->timer));
}

axis_handle_t axis_init(axis_id_t id)
{
    if (id >= AXIS_COUNT)
        return NULL;
    axis_t *ax = &s_axes[id];
    ax->cfg = &AXIS_CFG[id];
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << ax->cfg->pin_step) | (1ULL << ax->cfg->pin_dir) | (1ULL << ax->cfg->pin_ena),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE};
    ESP_ERROR_CHECK(gpio_config(&io));
    gpio_set_level(ax->cfg->pin_step, 0);
    gpio_set_level(ax->cfg->pin_dir, 0);
    gpio_set_level(ax->cfg->pin_ena, AXIS_ENA_ACTIVE);
    ax->move_done = xSemaphoreCreateBinary();
    gptimer_config_t tcfg = {.clk_src = GPTIMER_CLK_SRC_DEFAULT, .direction = GPTIMER_COUNT_UP, .resolution_hz = AXIS_TIMER_RES_HZ};
    ESP_ERROR_CHECK(gptimer_new_timer(&tcfg, &ax->timer));
    gptimer_event_callbacks_t cbs = {.on_alarm = axis_isr};
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(ax->timer, &cbs, ax));
    ESP_ERROR_CHECK(gptimer_enable(ax->timer));
    ax->state = AXIS_STATE_IDLE;
    ax->dir = AXIS_DIR_POS;
    ax->position = ax->speed_current = ax->speed_target = 0;
    ax->stop_request = ax->continuous = ax->step_high = false;
    if (id == AXIS_Z)
        g_axis_z = ax;
    if (id == AXIS_X)
        g_axis_x = ax;
    ESP_LOGI(TAG, "Os %s: STEP=%d DIR=%d ENA=%d | %.0f kr/mm",
             ax->cfg->name, ax->cfg->pin_step, ax->cfg->pin_dir, ax->cfg->pin_ena, ax->cfg->steps_per_mm);
    return ax;
}

void axis_stop(axis_handle_t ax)
{
    if (ax && ax->state != AXIS_STATE_IDLE)
        ax->stop_request = true;
}
void axis_enable(axis_handle_t ax, bool en)
{
    if (ax)
        gpio_set_level(ax->cfg->pin_ena, en ? AXIS_ENA_ACTIVE : AXIS_ENA_IDLE);
}

void axis_jog(axis_handle_t ax, axis_dir_t dir, uint16_t steps, uint8_t pct)
{
    if (!ax || ax->state != AXIS_STATE_IDLE || !steps)
        return;
    if (!limits_can_move(ax, dir))
        return;
    uint32_t s = ((uint32_t)ax->cfg->speed_max * pct) / 100;
    if (s < AXIS_SPEED_START)
        s = AXIS_SPEED_START;
    _start(ax, dir, s, steps, false);
}

void axis_run(axis_handle_t ax, axis_dir_t dir, float speed_mm_min)
{
    if (!ax)
        return;
    if (!limits_can_move(ax, dir))
        return;
    if (ax->state != AXIS_STATE_IDLE)
        axis_stop(ax);
    uint32_t s = (uint32_t)(speed_mm_min / 60.0f * ax->cfg->steps_per_mm);
    if (s < AXIS_SPEED_START)
        s = AXIS_SPEED_START;
    if (s > ax->cfg->speed_max)
        s = ax->cfg->speed_max;
    _start(ax, dir, s, 0, true);
}

bool axis_move(axis_handle_t ax, axis_move_t *mv)
{
    if (!ax || !mv || !mv->steps)
        return true;
    axis_dir_t dir = (mv->steps > 0) ? AXIS_DIR_POS : AXIS_DIR_NEG;
    if (!limits_can_move(ax, dir))
        return false;
    int32_t abs_s = mv->steps > 0 ? mv->steps : -mv->steps;
    uint32_t sps = mv->speed_mm_min > 0
                       ? (uint32_t)(mv->speed_mm_min / 60.0f * ax->cfg->steps_per_mm)
                       : ax->cfg->speed_max / 2;
    _start(ax, dir, sps, abs_s, false);
    // Timeout dynamiczny: 2× szacowany czas + minimum 5 sekund
    float est_s = (float)abs_s / (float)(sps > 0 ? sps : AXIS_SPEED_START);
    uint32_t timeout_ms = (uint32_t)(est_s * 2000.0f) + 5000;
    if (timeout_ms > 120000)
        timeout_ms = 120000; // max 2 minuty
    if (xSemaphoreTake(ax->move_done, pdMS_TO_TICKS(timeout_ms)) == pdTRUE)
        return true;
    axis_stop(ax);
    return false;
}

bool axis_move_to_mm(axis_handle_t ax, float target_mm, float speed_mm_min)
{
    if (!ax)
        return false;
    float delta = target_mm - axis_get_position_mm(ax);
    if (fabsf(delta) < 0.001f)
        return true;
    axis_move_t mv = {.steps = (int32_t)(delta * ax->cfg->steps_per_mm), .speed_mm_min = speed_mm_min};
    return axis_move(ax, &mv);
}

void axis_get_status(axis_handle_t ax, axis_status_t *st)
{
    if (!ax || !st)
        return;
    st->state = ax->state;
    st->dir = ax->dir;
    st->position_steps = ax->position;
    st->position_mm = (float)ax->position / ax->cfg->steps_per_mm;
    st->speed_steps_s = ax->speed_current;
    st->speed_mm_min = (float)ax->speed_current / ax->cfg->steps_per_mm * 60.0f;
}

float axis_get_position_mm(axis_handle_t ax) { return ax ? (float)ax->position / ax->cfg->steps_per_mm : 0.0f; }
int32_t axis_get_position_steps(axis_handle_t ax) { return ax ? ax->position : 0; }
axis_state_t axis_get_state(axis_handle_t ax) { return ax ? ax->state : AXIS_STATE_IDLE; }
uint32_t axis_get_speed(axis_handle_t ax) { return ax ? ax->speed_current : 0; }
void axis_reset_position(axis_handle_t ax)
{
    if (!ax)
        return;
    gptimer_stop(ax->timer);
    ax->position = 0;
    ax->stop_request = false;
    ax->state = AXIS_STATE_IDLE;
    ax->speed_current = 0;
    ESP_LOGI(TAG, "Os %s: zero", ax->cfg->name);
}

void axis_set_position(axis_handle_t ax, float pos_mm)
{
    if (!ax)
        return;
    gptimer_stop(ax->timer);
    ax->position = (int32_t)(pos_mm * ax->cfg->steps_per_mm);
    ax->stop_request = false;
    ax->state = AXIS_STATE_IDLE;
    ax->speed_current = 0;
    ESP_LOGI(TAG, "Os %s: pozycja=%.3f mm (%ld kr)", ax->cfg->name,
             pos_mm, (long)ax->position);
}

void axis_set_lead_mm(axis_handle_t ax, float lead_mm)
{
    if (!ax || lead_mm <= 0.0f || lead_mm > 20.0f)
        return;
    if (ax->cfg->id == AXIS_Z)
    {
        ax->cfg->steps_per_mm = (float)AXIS_Z_STEPS_PER_REV / lead_mm;
    }
    else
    {
        ax->cfg->steps_per_mm = (float)AXIS_X_STEPS_PER_REV / lead_mm;
    }
    ESP_LOGI(TAG, "Os %s: skok=%.2f mm → %.0f kr/mm", ax->cfg->name, lead_mm, ax->cfg->steps_per_mm);
}

void axis_set_max_speed_steps_s(axis_handle_t ax, uint32_t sps)
{
    if (!ax || sps < AXIS_SPEED_START)
        return;
    ax->cfg->speed_max = sps;
    ESP_LOGI(TAG, "Os %s: Vmax=%lu kr/s", ax->cfg->name, (unsigned long)sps);
}

float axis_get_steps_per_mm(axis_handle_t ax)
{
    return ax ? ax->cfg->steps_per_mm : 0.0f;
}
