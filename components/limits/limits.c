// limits.c – krańcówki mechaniczne NC + homing
#include "limits.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_attr.h"

static const char *TAG = "LIMITS";

// ----------------------------------------------------------
//  Konfiguracja krańcówek
// ----------------------------------------------------------
typedef struct {
    limit_id_t      id;
    const char     *name;
    int             gpio;
    axis_handle_t   axis;
    axis_dir_t      blocked_dir;
    volatile bool   triggered;
} limit_t;

static limit_t s_limits[LIMIT_COUNT] = {
    [LIMIT_Z_MIN] = { LIMIT_Z_MIN, "Z_MIN", LIMIT_Z_MIN_GPIO, NULL, AXIS_DIR_NEG, false },
    [LIMIT_Z_MAX] = { LIMIT_Z_MAX, "Z_MAX", LIMIT_Z_MAX_GPIO, NULL, AXIS_DIR_POS, false },
    [LIMIT_X_MIN] = { LIMIT_X_MIN, "X_MIN", LIMIT_X_MIN_GPIO, NULL, AXIS_DIR_NEG, false },
    [LIMIT_X_MAX] = { LIMIT_X_MAX, "X_MAX", LIMIT_X_MAX_GPIO, NULL, AXIS_DIR_POS, false },
};

static void (*s_callback)(limit_id_t, void*) = NULL;
static void *s_callback_arg = NULL;
static SemaphoreHandle_t s_mutex = NULL;

// Czy oś Z/X została zhomowana (referencjonowana)
static volatile bool s_homed[AXIS_COUNT] = {false, false};

// Oś jest właśnie bazowana – wolno wtedy ruszać w obie strony (zjazd z krańcówki,
// cofnięcie po dojechaniu), mimo że s_homed jeszcze == false.
static volatile bool s_homing[AXIS_COUNT] = {false, false};

// Czy limits_init() zostało wykonane (piny skonfigurowane)
static bool s_inited = false;

// Aktualny, "żywy" stan krańcówki NC: rozwarta (albo zerwany przewód) = HIGH.
// Decyzje o blokadzie ruchu podejmujemy na podstawie TEGO, a nie zatrzaśniętej flagi
// `triggered` – pojedynczy impuls zakłócenia nie blokuje wtedy ruchu do restartu.
static inline bool limit_open(const limit_t *lim)
{
    return s_inited && gpio_get_level(lim->gpio) == 1;
}

// GPIO46 w ESP32-S3 jest tylko wejściem i nie ma wewnętrznego pull-up
// (wymaga rezystora zewnętrznego do 3,3 V).
static inline bool has_internal_pullup(int gpio)
{
    return gpio != 46;
}

// ----------------------------------------------------------
//  ISR – wspólny handler
// ----------------------------------------------------------
static void IRAM_ATTR limit_isr(void *arg)
{
    limit_t *lim = (limit_t *)arg;
    if (lim->axis) {
        axis_stop(lim->axis);
    }
    lim->triggered = true;
    if (s_callback) {
        s_callback(lim->id, s_callback_arg);
    }
}

// ----------------------------------------------------------
//  API
// ----------------------------------------------------------
void limits_init(void)
{
    s_mutex = xSemaphoreCreateMutex();

    s_limits[LIMIT_Z_MIN].axis = g_axis_z;
    s_limits[LIMIT_Z_MAX].axis = g_axis_z;
    s_limits[LIMIT_X_MIN].axis = g_axis_x;
    s_limits[LIMIT_X_MAX].axis = g_axis_x;

    // gpio_isr_service już zainstalowane przez encoder_init()

    for (int i = 0; i < LIMIT_COUNT; i++) {
        limit_t *lim = &s_limits[i];
        gpio_config_t cfg = {
            .pin_bit_mask = (1ULL << lim->gpio),
            .mode         = GPIO_MODE_INPUT,
            .pull_up_en   = has_internal_pullup(lim->gpio) ? GPIO_PULLUP_ENABLE
                                                           : GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_POSEDGE,   // NC → rozwarcie = HIGH
        };
        ESP_ERROR_CHECK(gpio_config(&cfg));
        ESP_ERROR_CHECK(gpio_isr_handler_add(lim->gpio, limit_isr, lim));
        lim->triggered = (gpio_get_level(lim->gpio) == 1);  // HIGH = wyzwolona
        ESP_LOGI(TAG, "%s: GPIO%d, %s",
                 lim->name, lim->gpio,
                 lim->triggered ? "WYZWOLONA!" : "OK");
    }
    s_inited = true;
    ESP_LOGI(TAG, "Krancowki gotowe (%d szt.)", LIMIT_COUNT);
}

bool limits_is_triggered(limit_id_t id) {
    return (id < LIMIT_COUNT) ? limit_open(&s_limits[id]) : false;
}

void limits_get_status(limit_id_t id, limit_status_t *st) {
    if (id >= LIMIT_COUNT || !st) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    st->id = s_limits[id].id;
    st->triggered = limit_open(&s_limits[id]);
    st->name = s_limits[id].name;
    xSemaphoreGive(s_mutex);
}

bool limits_any_triggered(void) {
    for (int i = 0; i < LIMIT_COUNT; i++)
        if (limit_open(&s_limits[i])) return true;
    return false;
}

bool limits_can_move(axis_handle_t ax, axis_dir_t dir) {
    if (!ax) return false;

    axis_id_t aid = (ax == g_axis_z) ? AXIS_Z : AXIS_X;

    // Czy limits_init() zostało wywołane
    bool limits_active = s_inited;

    // Jeśli oś nie jest zhomowana, dozwolony tylko ruch w stronę krańcówki home.
    // Wyjątek: trwa bazowanie tej osi (zjazd z krańcówki i cofnięcie muszą działać).
    if (limits_active && !s_homed[aid] && !s_homing[aid]) {
        // Z_MIN → blocked_dir=AXIS_DIR_NEG, więc home = AXIS_DIR_NEG
        // X_MIN → blocked_dir=AXIS_DIR_NEG, więc home = AXIS_DIR_NEG
        // Dozwolony tylko ten kierunek
        if (dir != AXIS_DIR_NEG) {
            ESP_LOGW(TAG, "Os %s nie zhomowana – dozwolony tylko ruch w strone home",
                     aid == AXIS_Z ? "Z" : "X");
            return false;
        }
    }

    // Sprawdź czy nie wyzwolona krańcówka blokuje ten kierunek
    for (int i = 0; i < LIMIT_COUNT; i++) {
        const limit_t *lim = &s_limits[i];
        if (lim->axis == ax && lim->blocked_dir == dir && limit_open(lim)) {
            ESP_LOGW(TAG, "%s: %s wyzwolona, kierunek zablokowany",
                     aid == AXIS_Z ? "Z" : "X", lim->name);
            return false;
        }
    }

    // ── Software limits: always active after homing (even without HW switches) ──
    if (s_homed[aid]) {
        float pos = axis_get_position_mm(ax);
        float smin = (aid == AXIS_Z) ? SOFT_LIMIT_Z_MIN_MM : SOFT_LIMIT_X_MIN_MM;
        float smax = (aid == AXIS_Z) ? SOFT_LIMIT_Z_MAX_MM : SOFT_LIMIT_X_MAX_MM;
        if ((dir == AXIS_DIR_NEG && pos <= smin + 0.01f) ||
            (dir == AXIS_DIR_POS && pos >= smax - 0.01f)) {
            ESP_LOGW(TAG, "Os %s: soft-limit (%.2f mm, limit: %.0f..%.0f)",
                     aid == AXIS_Z ? "Z" : "X", pos, smin, smax);
            return false;
        }
    }

    return true;
}

static bool home_axis_impl(axis_handle_t ax, limit_id_t home_switch,
                           axis_dir_t approach_dir, float speed_mm_min) {
    if (!ax || home_switch >= LIMIT_COUNT) return false;
    limit_t *lim = &s_limits[home_switch];
    if (lim->axis != ax) return false;

    axis_id_t aid = (ax == g_axis_z) ? AXIS_Z : AXIS_X;
    ESP_LOGI(TAG, "Homing %s → %s, %.0f mm/min",
             lim->axis == g_axis_z ? "Z" : "X", lim->name, speed_mm_min);

    // Jeśli krańcówka już wyzwolona (sanki na niej stoją) – najpierw odjedź
    if (limit_open(lim)) {
        ESP_LOGI(TAG, "%s: os na krancowce, odjezdzam...", lim->name);
        axis_dir_t away = (approach_dir == AXIS_DIR_POS) ? AXIS_DIR_NEG : AXIS_DIR_POS;
        axis_run(ax, away, speed_mm_min / 2.0f);
        vTaskDelay(pdMS_TO_TICKS(500));
        // Jeśli po 500ms wciąż wyzwolona, jedź dalej aż do zwolnienia
        int w = 0;
        while (limit_open(lim) && w < 100) {
            vTaskDelay(pdMS_TO_TICKS(100));
            w++;
        }
        axis_stop(ax);
        vTaskDelay(pdMS_TO_TICKS(100));
        lim->triggered = false;
        if (w >= 100) {
            ESP_LOGE(TAG, "Homing %s: nie moge odjechac od krancowki", lim->name);
            return false;
        }
        ESP_LOGI(TAG, "%s: odjechalem, podejde ponownie...", lim->name);
    }

    // Podejście do krańcówki
    lim->triggered = false;
    axis_run(ax, approach_dir, speed_mm_min);

    int timeout = 0;
    while (!lim->triggered && timeout < 300) {
        if (limit_open(lim)) {  // NC rozwarty = HIGH
            lim->triggered = true;
            axis_stop(ax);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
        timeout++;
    }
    axis_stop(ax);
    vTaskDelay(pdMS_TO_TICKS(100));

    if (!lim->triggered) {
        ESP_LOGE(TAG, "Homing %s: timeout", lim->name);
        return false;
    }

    // Cofnij 2 mm od krańcówki
    axis_dir_t back = (approach_dir == AXIS_DIR_POS) ? AXIS_DIR_NEG : AXIS_DIR_POS;
    axis_move_t mv = {
        .steps = (int32_t)(2.0f * (ax == g_axis_z ? AXIS_Z_STEPS_PER_MM : AXIS_X_STEPS_PER_MM)),
        .speed_mm_min = speed_mm_min / 2.0f,
    };
    if (back == AXIS_DIR_NEG) mv.steps = -mv.steps;
    axis_move(ax, &mv);
    if (limit_open(lim)) {
        ESP_LOGW(TAG, "%s: po cofnieciu o 2 mm krancowka wciaz rozwarta – sprawdz histereze/montaz",
                 lim->name);
    }

    axis_reset_position(ax);
    lim->triggered = false;
    s_homed[aid] = true;
    ESP_LOGI(TAG, "Homing %s: OK, pozycja=0 [ZHOMOWANO]", lim->name);
    return true;
}

bool limits_home_axis(axis_handle_t ax, limit_id_t home_switch,
                      axis_dir_t approach_dir, float speed_mm_min) {
    if (!ax || home_switch >= LIMIT_COUNT || !s_inited) return false;
    axis_id_t aid = (ax == g_axis_z) ? AXIS_Z : AXIS_X;
    s_homing[aid] = true;
    bool ok = home_axis_impl(ax, home_switch, approach_dir, speed_mm_min);
    s_homing[aid] = false;
    return ok;
}

void limits_register_callback(void (*cb)(limit_id_t, void*), void *arg) {
    s_callback = cb;
    s_callback_arg = arg;
}

bool limits_axis_homed(axis_id_t axis_id) {
    return (axis_id < AXIS_COUNT) ? s_homed[axis_id] : false;
}
void limits_set_axis_homed(axis_id_t axis_id, bool homed) {
    if (axis_id < AXIS_COUNT) s_homed[axis_id] = homed;
}
bool limits_all_homed(void) {
    return s_homed[AXIS_Z] && s_homed[AXIS_X];
}

void limits_clear(limit_id_t id) {
    if (id >= LIMIT_COUNT) return;
    if (!limit_open(&s_limits[id])) {  // LOW = NC zwarty
        s_limits[id].triggered = false;
        ESP_LOGI(TAG, "%s wyczyszczona", s_limits[id].name);
    }
}