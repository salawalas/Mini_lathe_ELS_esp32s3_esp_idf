// motion.c – Electronic Lead Screw (ELS)
// v2 – ISR-safe: spindle_rev_cb tylko inkrementuje akumulator,
//      właściwy ruch osi Z wykonuje els_task_fn.
#include "motion.h"
#include <math.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_timer.h"

static const char *TAG = "ELS";

const thread_preset_t ELS_THREAD_PRESETS[] = {
    {"M3  x0.5",  THREAD_METRIC,   0.50f}, {"M4  x0.7",  THREAD_METRIC,   0.70f},
    {"M5  x0.8",  THREAD_METRIC,   0.80f}, {"M6  x1.0",  THREAD_METRIC,   1.00f},
    {"M8  x1.25", THREAD_METRIC,   1.25f}, {"M10 x1.5",  THREAD_METRIC,   1.50f},
    {"M12 x1.75", THREAD_METRIC,   1.75f}, {"M14 x2.0",  THREAD_METRIC,   2.00f},
    {"M16 x2.0",  THREAD_METRIC,   2.00f}, {"M20 x2.5",  THREAD_METRIC,   2.50f},
    {"M24 x3.0",  THREAD_METRIC,   3.00f}, {"1/4-20 UNC",THREAD_IMPERIAL, 20.0f},
    {"5/16-18",   THREAD_IMPERIAL, 18.0f}, {"3/8-16 UNC",THREAD_IMPERIAL, 16.0f},
    {"1/2-13 UNC",THREAD_IMPERIAL, 13.0f}, {"1/4-28 UNF",THREAD_IMPERIAL, 28.0f},
    {"Tr10 x2",   THREAD_METRIC,   2.00f}, {"Tr12 x3",   THREAD_METRIC,   3.00f},
    {"Tr16 x4",   THREAD_METRIC,   4.00f},
    // BSP (British Standard Pipe) – TPI
    {"G 1/8-28",  THREAD_IMPERIAL, 28.0f}, {"G 1/4-19",  THREAD_IMPERIAL, 19.0f},
    {"G 3/8-19",  THREAD_IMPERIAL, 19.0f}, {"G 1/2-14",  THREAD_IMPERIAL, 14.0f},
    {"G 3/4-14",  THREAD_IMPERIAL, 14.0f}, {"G 1-11",    THREAD_IMPERIAL, 11.0f},
    // NPT (National Pipe Taper) – TPI
    {"1/8-27 NPT",THREAD_IMPERIAL, 27.0f}, {"1/4-18 NPT",THREAD_IMPERIAL, 18.0f},
    {"3/8-18 NPT",THREAD_IMPERIAL, 18.0f}, {"1/2-14 NPT",THREAD_IMPERIAL, 14.0f},
    {"3/4-14 NPT",THREAD_IMPERIAL, 14.0f},
};
const uint8_t ELS_THREAD_PRESETS_COUNT = sizeof(ELS_THREAD_PRESETS)/sizeof(ELS_THREAD_PRESETS[0]);

static struct {
    els_state_t     state;
    els_config_t    cfg;
    float           feed_steps_per_rev;
    volatile float  accumulator;
    volatile int32_t steps_sent;
    uint8_t         pass_current;
    volatile bool   active, rev_pending, stop_request;
    SemaphoreHandle_t mutex;
    TaskHandle_t    task_handle;
} e = {0};

static TaskHandle_t els_notify_target = NULL;

float els_tpi_to_mm(float tpi) { return 25.4f / tpi; }
float els_feed_steps_per_spindle_rev(float pitch_mm) { return pitch_mm * STEPPER_STEPS_PER_MM; }
float els_feed_steps_per_spindle_rev_runtime(float pitch_mm) { return pitch_mm * axis_get_steps_per_mm(g_axis_z); }

// ── ISR-safe callback – wywoływany z spindle_isr co 1 obrót wrzeciona ──
// NIE wolno tu wołać stepper_jog / axis_jog / xSemaphoreTake
static void IRAM_ATTR spindle_rev_cb(void *arg)
{
    if (!e.active || e.stop_request) return;

    e.accumulator += e.feed_steps_per_rev;
    e.rev_pending = true;

    BaseType_t hp = pdFALSE;
    if (els_notify_target) {
        vTaskNotifyGiveFromISR(els_notify_target, &hp);
    }
    if (hp) portYIELD_FROM_ISR();
}

// ── Task ELS – konsumuje impulsy wrzeciona i steruje osią Z ──
static void els_task_fn(void *arg)
{
    els_notify_target = xTaskGetCurrentTaskHandle();

    for (uint8_t pass = 1; pass <= e.cfg.passes; pass++) {
        if (e.stop_request) break;
        e.pass_current = pass;
        e.steps_sent   = 0;
        e.accumulator  = 0.0f;
        e.rev_pending  = false;

        // ── Dosuw X (głębokość skrawania) ──
        // Odczyt X co przejście — odporny na ręczne korekty i błędy poprzedniego przejścia
        if (e.cfg.depth_per_pass > 0.0f && g_axis_x) {
            float target_x = axis_get_position_mm(g_axis_x) - e.cfg.depth_per_pass;
            ESP_LOGI(TAG, "Przejscie %d/%d: X → %.3f mm", pass, e.cfg.passes, target_x);
            if (!axis_move_to_mm(g_axis_x, target_x, 30.0f)) {
                ESP_LOGW(TAG, "Blad dosuwu X w przejsciu %d", pass);
            }
        }

        // ── Powrót Z na start ──
        float cur = stepper_get_position_mm();
        if (fabsf(cur - e.cfg.z_start_mm) > 0.02f) {
            e.state = ELS_STATE_RETRACTING;
            stepper_dir_t rd = (cur > e.cfg.z_start_mm) ? STEPPER_DIR_CCW : STEPPER_DIR_CW;
            float spmm_z = axis_get_steps_per_mm(g_axis_z);
            stepper_move_t mv = {
                .steps = (int32_t)(fabsf(cur - e.cfg.z_start_mm) * spmm_z),
                .speed_mm_min = 300.0f
            };
            if (rd == STEPPER_DIR_CCW) mv.steps = -mv.steps;
            stepper_move(&mv);
        }
        if (e.stop_request) break;

        // ── Czekaj na stabilizację RPM ──
        e.state = ELS_STATE_WAITING;
        int w = 0;
        while (!spindle_is_at_speed() && w < 100) {
            vTaskDelay(pdMS_TO_TICKS(50));
            w++;
            if (e.stop_request) goto done;
        }
        if (!spindle_is_at_speed()) {
            e.state = ELS_STATE_ERROR;
            goto done;
        }

        e.state        = ELS_STATE_RUNNING;
        e.active       = true;
        e.rev_pending  = false;
        e.accumulator  = 0.0f;

        // ── Główna pętla: odbieraj powiadomienia z ISR i konsumuj kroki ──
        for (;;) {
            uint32_t n = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500));
            if (!n && !e.rev_pending) break;  // timeout = koniec przejścia

            while (e.accumulator >= 1.0f && !e.stop_request) {
                int32_t steps = (int32_t)e.accumulator;
                if (steps > 160) steps = 160;  // batch limit – nie blokuj
                e.accumulator -= (float)steps;
                if (steps <= 0) break;

                float cur_z = stepper_get_position_mm();
                float rem = (e.cfg.feed_dir == STEPPER_DIR_CW)
                            ? e.cfg.z_end_mm - cur_z
                            : cur_z - e.cfg.z_end_mm;
                if (rem <= 0.0f) {
                    e.active = false; e.stop_request = true; stepper_stop(); break;
                }

                float max_steps_f = rem * axis_get_steps_per_mm(g_axis_z);
                if ((float)steps > max_steps_f) {
                    steps = (int32_t)max_steps_f;
                    if (steps <= 0) { e.active = false; e.stop_request = true; stepper_stop(); break; }
                }

                stepper_jog(e.cfg.feed_dir, (uint16_t)steps, 90);
                e.steps_sent += steps;
            }
            if (!e.active || e.stop_request) break;
            e.rev_pending = false;
        }

        e.active = false;
        stepper_stop();
        if (e.stop_request) break;
        if (pass < e.cfg.passes) vTaskDelay(pdMS_TO_TICKS(300));
    }

done:
    e.active       = false;
    e.stop_request = false;
    e.state        = ELS_STATE_IDLE;
    e.task_handle  = NULL;
    els_notify_target = NULL;
    vTaskDelete(NULL);
}

void els_init(void)
{
    e.mutex = xSemaphoreCreateMutex();
    spindle_register_step_callback(SPINDLE_STEPS_PER_WREV, spindle_rev_cb, NULL);
    ESP_LOGI(TAG, "ELS gotowy. %d presetow gwintow.", ELS_THREAD_PRESETS_COUNT);
}

bool els_start(const els_config_t *cfg)
{
    if (e.state != ELS_STATE_IDLE) return false;
    if (cfg->pitch_mm <= 0.0f || fabsf(cfg->z_end_mm - cfg->z_start_mm) < 0.1f) return false;
    memcpy(&e.cfg, cfg, sizeof(els_config_t));
    e.feed_steps_per_rev = els_feed_steps_per_spindle_rev_runtime(cfg->pitch_mm);
    e.accumulator  = 0.0f;
    e.stop_request = false;
    e.active       = false;
    e.rev_pending  = false;
    xTaskCreate(els_task_fn, "els_task", 4096, NULL, 6, &e.task_handle);
    return true;
}

void els_stop(void)
{
    if (e.state == ELS_STATE_IDLE) return;
    e.active       = false;
    e.stop_request = true;
    stepper_stop();
    if (e.task_handle) xTaskNotifyGive(e.task_handle);
}

void els_get_status(els_status_t *st)
{
    st->state        = e.state;
    st->pitch_mm     = e.cfg.pitch_mm;
    st->pass_current = e.pass_current;
    st->pass_total   = e.cfg.passes;
    st->z_pos_mm     = stepper_get_position_mm();
    st->spindle_rpm  = spindle_get_rpm();
    st->steps_sent   = e.steps_sent;
    st->sync_ok      = e.active && (e.state == ELS_STATE_RUNNING) && spindle_is_at_speed();
}

bool els_is_running(void)
{
    return e.state == ELS_STATE_RUNNING
        || e.state == ELS_STATE_WAITING
        || e.state == ELS_STATE_RETRACTING;
}