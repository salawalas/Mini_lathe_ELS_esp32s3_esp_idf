// spindle.c – wrzeciono krokowe NEMA23 + DM556 #1 + MOSFET E-STOP
#include "spindle.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gptimer.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_attr.h"

static const char *TAG = "SPINDLE";

#define TIMER_ALARM_MIN_US  10UL
#define TIMER_ALARM_MAX_US  (1000000UL / SPINDLE_SPEED_START)

static uint16_t s_rpm_max = SPINDLE_RPM_MAX;

static struct {
    volatile spindle_state_t state;
    volatile spindle_dir_t   dir;
    volatile uint32_t        speed_current, speed_target;
    volatile int32_t         position;
    volatile bool            step_high, stop_request, estop_active, power_enabled;
    uint32_t  cb_steps, cb_counter;
    void    (*cb)(void*);   void *cb_arg;
    void    (*estop_cb)(void*); void *estop_cb_arg;
    uint16_t  rpm_target;
    SemaphoreHandle_t mutex;
} s = {0};

static gptimer_handle_t s_timer = NULL;

static inline uint64_t IRAM_ATTR s2a(uint32_t sps)
{
    if (!sps) return TIMER_ALARM_MAX_US;
    uint64_t p = 1000000UL / sps;
    if (p < TIMER_ALARM_MIN_US) p = TIMER_ALARM_MIN_US;
    if (p > TIMER_ALARM_MAX_US) p = TIMER_ALARM_MAX_US;
    return p;
}

static void IRAM_ATTR estop_isr(void *arg)
{
    gpio_set_level(SPINDLE_POWER_PIN, 0);
    s.power_enabled = false;
    s.stop_request  = true;
    s.estop_active  = true;
    s.state         = SPINDLE_STATE_ESTOP;
    if (s.estop_cb) s.estop_cb(s.estop_cb_arg);
    portYIELD_FROM_ISR();
}

static bool IRAM_ATTR spindle_isr(gptimer_handle_t timer,
                                   const gptimer_alarm_event_data_t *e, void *ctx)
{
    BaseType_t hp = pdFALSE;
    if (s.stop_request) {
        gpio_set_level(SPINDLE_PIN_STEP, 0);
        s.step_high = false; s.speed_current = 0; s.stop_request = false;
        if (s.state != SPINDLE_STATE_ESTOP) s.state = SPINDLE_STATE_STOPPED;
        gptimer_stop(timer); return hp;
    }
    s.step_high = !s.step_high;
    gpio_set_level(SPINDLE_PIN_STEP, s.step_high ? 1 : 0);
    if (s.step_high) {
        s.position += (s.dir == SPINDLE_DIR_FWD) ? 1 : -1;
        if (s.cb && s.cb_steps) {
            if (++s.cb_counter >= s.cb_steps) { s.cb_counter = 0; s.cb(s.cb_arg); }
        }
        if (s.speed_current < s.speed_target) {
            s.state = SPINDLE_STATE_RAMPING_UP;
            uint32_t d = SPINDLE_ACCEL / s.speed_current + 1;
            s.speed_current += d;
            if (s.speed_current > s.speed_target) s.speed_current = s.speed_target;
        } else if (s.speed_current > s.speed_target) {
            s.state = SPINDLE_STATE_RAMPING_DOWN;
            uint32_t d = SPINDLE_ACCEL / s.speed_current + 1;
            if (d >= s.speed_current - SPINDLE_SPEED_START) s.speed_current = SPINDLE_SPEED_START;
            else s.speed_current -= d;
            if (!s.speed_target && s.speed_current <= SPINDLE_SPEED_START) {
                gpio_set_level(SPINDLE_PIN_STEP, 0);
                s.step_high = false; s.state = SPINDLE_STATE_STOPPED; s.speed_current = 0;
                gptimer_stop(timer); return hp;
            }
        } else { s.state = SPINDLE_STATE_RUNNING; }
    }
    uint64_t half = s2a(s.speed_current) / 2;
    if (half < TIMER_ALARM_MIN_US) half = TIMER_ALARM_MIN_US;
    gptimer_alarm_config_t alarm = { .alarm_count = e->alarm_value + half, .flags.auto_reload_on_alarm=false };
    gptimer_set_alarm_action(timer, &alarm);
    return hp;
}

void spindle_init(void)
{
    s.mutex = xSemaphoreCreateMutex();

    gpio_config_t out = {
        .pin_bit_mask = (1ULL<<SPINDLE_PIN_STEP)|(1ULL<<SPINDLE_PIN_DIR)|
                        (1ULL<<SPINDLE_PIN_ENA) |(1ULL<<SPINDLE_POWER_PIN),
        .mode=GPIO_MODE_OUTPUT, .pull_up_en=GPIO_PULLUP_DISABLE,
        .pull_down_en=GPIO_PULLDOWN_DISABLE, .intr_type=GPIO_INTR_DISABLE };
    ESP_ERROR_CHECK(gpio_config(&out));
    gpio_set_level(SPINDLE_PIN_STEP, 0); gpio_set_level(SPINDLE_PIN_DIR, 0);
    gpio_set_level(SPINDLE_PIN_ENA, SPINDLE_ENA_ACTIVE);
    gpio_set_level(SPINDLE_POWER_PIN, 0);

    gpio_config_t estop_cfg = {
        .pin_bit_mask=(1ULL<<SPINDLE_ESTOP_PIN), .mode=GPIO_MODE_INPUT,
        .pull_up_en=GPIO_PULLUP_ENABLE, .pull_down_en=GPIO_PULLDOWN_DISABLE,
        .intr_type=GPIO_INTR_NEGEDGE };
    ESP_ERROR_CHECK(gpio_config(&estop_cfg));
    esp_err_t r = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (r != ESP_OK && r != ESP_ERR_INVALID_STATE)
    {
        ESP_ERROR_CHECK(r); // ESP_ERR_INVALID_STATE = już zainstalowane = OK
    }
    gpio_isr_handler_add(SPINDLE_ESTOP_PIN, estop_isr, NULL);

    gptimer_config_t tcfg = { .clk_src=GPTIMER_CLK_SRC_DEFAULT,
                               .direction=GPTIMER_COUNT_UP, .resolution_hz=1000000UL };
    ESP_ERROR_CHECK(gptimer_new_timer(&tcfg, &s_timer));
    gptimer_event_callbacks_t cbs = { .on_alarm=spindle_isr };
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(s_timer, &cbs, NULL));
    ESP_ERROR_CHECK(gptimer_enable(s_timer));
    ESP_LOGI(TAG, "Wrzeciono OK. Przekladnia 1:%.0f, %lu kr/obr wrzeciona",
             SPINDLE_GEAR_RATIO, (unsigned long)SPINDLE_STEPS_PER_WREV);
}

void spindle_power_enable(bool en)
{
    if (s.estop_active && en) { ESP_LOGW(TAG,"E-STOP aktywny!"); return; }
    gpio_set_level(SPINDLE_POWER_PIN, en ? 1 : 0);
    s.power_enabled = en;
}

void spindle_start(uint16_t rpm, spindle_dir_t dir)
{
    if (s.estop_active) { ESP_LOGW(TAG,"E-STOP!"); return; }
    if (!rpm) { spindle_stop(); return; }
    if (rpm > s_rpm_max) rpm = s_rpm_max;
    if (rpm < SPINDLE_RPM_MIN) rpm = SPINDLE_RPM_MIN;
    if (!s.power_enabled) { spindle_power_enable(true); vTaskDelay(pdMS_TO_TICKS(50)); }
    uint32_t sps = SPINDLE_RPM_TO_SPS(rpm);
    s.rpm_target = rpm; s.dir = dir; s.stop_request = false; s.cb_counter = 0;
    gpio_set_level(SPINDLE_PIN_DIR, (dir==SPINDLE_DIR_FWD)?1:0);
    esp_rom_delay_us(10);
    if (s.state != SPINDLE_STATE_STOPPED) { s.speed_target = sps; return; }
    s.speed_current = SPINDLE_SPEED_START; s.speed_target = sps;
    s.state = SPINDLE_STATE_RAMPING_UP; s.step_high = false;
    uint64_t half = s2a(SPINDLE_SPEED_START) / 2;
    gptimer_alarm_config_t alarm = { .alarm_count=half, .flags.auto_reload_on_alarm=false };
    ESP_ERROR_CHECK(gptimer_set_raw_count(s_timer, 0));
    ESP_ERROR_CHECK(gptimer_set_alarm_action(s_timer, &alarm));
    ESP_ERROR_CHECK(gptimer_start(s_timer));
}

void spindle_stop(void)
{
    if (s.state==SPINDLE_STATE_STOPPED||s.state==SPINDLE_STATE_ESTOP) return;
    s.rpm_target=0; s.speed_target=0;
}

void spindle_emergency_stop(void)
{
    gpio_set_level(SPINDLE_POWER_PIN, 0);
    s.power_enabled=false; s.stop_request=true; s.state=SPINDLE_STATE_ESTOP;
}

void spindle_set_rpm(uint16_t rpm)
{
    if (s.estop_active) return;
    if (s.state==SPINDLE_STATE_STOPPED) { spindle_start(rpm,s.dir); return; }
    if (rpm > s_rpm_max) rpm = s_rpm_max;
    if (rpm < SPINDLE_RPM_MIN) rpm = SPINDLE_RPM_MIN;
    s.rpm_target=rpm; s.speed_target=SPINDLE_RPM_TO_SPS(rpm);
}

void spindle_get_status(spindle_status_t *st)
{
    xSemaphoreTake(s.mutex, portMAX_DELAY);
    st->state=s.state; st->dir=s.dir; st->rpm_target=s.rpm_target;
    st->steps_per_s=s.speed_current; st->rpm_actual=SPINDLE_SPS_TO_RPM(s.speed_current);
    st->position=s.position; st->power_enabled=s.power_enabled; st->estop_active=s.estop_active;
    uint32_t tgt=SPINDLE_RPM_TO_SPS(s.rpm_target), margin=tgt/20;
    st->at_speed=(s.state==SPINDLE_STATE_RUNNING)&&(s.speed_current>=tgt-margin)&&(s.speed_current<=tgt+margin);
    xSemaphoreGive(s.mutex);
}

uint16_t spindle_get_rpm(void)
{
    xSemaphoreTake(s.mutex, portMAX_DELAY);
    uint16_t rpm = SPINDLE_SPS_TO_RPM(s.speed_current);
    xSemaphoreGive(s.mutex);
    return rpm;
}
bool spindle_is_at_speed(void)
{
    xSemaphoreTake(s.mutex, portMAX_DELAY);
    bool result = false;
    if (s.state == SPINDLE_STATE_RUNNING) {
        uint32_t t = SPINDLE_RPM_TO_SPS(s.rpm_target), m = t / 20;
        result = (s.speed_current >= t - m && s.speed_current <= t + m);
    }
    xSemaphoreGive(s.mutex);
    return result;
}
int32_t spindle_get_position(void) { return s.position; }
void spindle_reset_position(void)  { s.position=0; s.cb_counter=0; }
bool spindle_power_is_enabled(void){ return s.power_enabled; }
bool spindle_estop_is_active(void) { return s.estop_active; }
void spindle_estop_reset(void)
{
    if (gpio_get_level(SPINDLE_ESTOP_PIN)==0) { ESP_LOGW(TAG,"Wylacznik wciaz wcisniety!"); return; }
    s.estop_active=false; s.state=SPINDLE_STATE_STOPPED;
}
void spindle_register_step_callback(uint32_t n, void (*cb)(void*), void *arg)
    { s.cb_steps=n; s.cb=cb; s.cb_arg=arg; s.cb_counter=0; }
void spindle_register_estop_callback(void (*cb)(void*), void *arg)
    { s.estop_cb=cb; s.estop_cb_arg=arg; }

void spindle_set_max_rpm(uint16_t rpm)
{
    s_rpm_max = rpm;
    ESP_LOGI(TAG, "RPM max = %u", rpm);
}
