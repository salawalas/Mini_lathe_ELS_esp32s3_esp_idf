// touch.c – Generic touch dispatcher (XPT2046 / FT6x06 / GT911)
#include "touch.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include <string.h>
#include <math.h>

static const char *TAG = "TOUCH";

// ── Weak stubs (overridden by driver files) ────────────────
extern esp_err_t touch_xpt2046_init(void);
extern esp_err_t touch_xpt2046_read_raw(touch_raw_t *out);

extern esp_err_t touch_ft6x06_init(void);
extern esp_err_t touch_ft6x06_read_raw(touch_raw_t *out);

static touch_type_t s_type = TOUCH_NONE;
static touch_calib_t s_cal = {0};
static bool s_cal_loaded = false;

esp_err_t touch_init(void)
{
#if CONFIG_TOUCH_TYPE_XPT2046
    s_type = TOUCH_XPT2046;
    esp_err_t ret = touch_xpt2046_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "XPT2046 init failed");
        return ret;
    }
#elif CONFIG_TOUCH_TYPE_FT6X06
    s_type = TOUCH_FT6X06;
    esp_err_t ret = touch_ft6x06_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FT6x06 init failed");
        return ret;
    }
#else
    ESP_LOGI(TAG, "Touch disabled (menuconfig)");
    return ESP_ERR_NOT_SUPPORTED;
#endif

    // Load calibration from NVS
    if (touch_calib_load_from_nvs(&s_cal)) {
        touch_calib_apply(&s_cal);
        s_cal_loaded = true;
        ESP_LOGI(TAG, "Kalibracja wczytana z NVS");
    }

    ESP_LOGI(TAG, "Touch init OK (type=%d)", s_type);
    return ESP_OK;
}

esp_err_t touch_read_raw(touch_raw_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;

    switch (s_type) {
    case TOUCH_XPT2046: return touch_xpt2046_read_raw(out);
    case TOUCH_FT6X06:  return touch_ft6x06_read_raw(out);
    default:            return ESP_ERR_NOT_SUPPORTED;
    }
}

esp_err_t touch_read(touch_point_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;

    touch_raw_t raw;
    esp_err_t ret = touch_read_raw(&raw);
    if (ret != ESP_OK) return ret;

    out->pressed = (raw.z > 0);

    if (s_cal_loaded) {
        out->x = (uint16_t)(raw.x * s_cal.scale_x + s_cal.offset_x);
        out->y = (uint16_t)(raw.y * s_cal.scale_y + s_cal.offset_y);
    } else {
        out->x = raw.x;
        out->y = raw.y;
    }
    return ESP_OK;
}

bool touch_is_pressed(void)
{
    touch_raw_t raw;
    if (touch_read_raw(&raw) != ESP_OK) return false;
    return raw.z > 0;
}

// ── Kalibracja 3-punktowa ──────────────────────────────────
void touch_calib_reset(touch_calib_t *cal)
{
    memset(cal, 0, sizeof(*cal));
    cal->scale_x = 1.0f;
    cal->scale_y = 1.0f;
}

void touch_calib_add_point(touch_calib_t *cal, const touch_raw_t *raw,
                           uint16_t ref_x, uint16_t ref_y)
{
    for (int i = 0; i < 3; i++) {
        if (cal->ref_x[i] == 0 && cal->ref_y[i] == 0) {
            cal->raw[i]  = *raw;
            cal->ref_x[i] = ref_x;
            cal->ref_y[i] = ref_y;
            ESP_LOGI(TAG, "Cal point %d: raw(%d,%d) → ref(%d,%d)",
                     i, raw->x, raw->y, ref_x, ref_y);
            return;
        }
    }
}

bool touch_calib_compute(touch_calib_t *cal)
{
    // Simple linear regression on 3 points
    float sum_rx = 0, sum_ry = 0, sum_ex = 0, sum_ey = 0;
    for (int i = 0; i < 3; i++) {
        sum_rx += cal->raw[i].x;
        sum_ry += cal->raw[i].y;
        sum_ex += cal->ref_x[i];
        sum_ey += cal->ref_y[i];
    }

    // Scale = ratio of deltas
    float dr_x = (cal->raw[2].x - cal->raw[0].x);
    float dr_y = (cal->raw[2].y - cal->raw[0].y);
    float de_x = (float)(cal->ref_x[2] - cal->ref_x[0]);
    float de_y = (float)(cal->ref_y[2] - cal->ref_y[0]);

    cal->scale_x  = (dr_x != 0) ? de_x / dr_x : 1.0f;
    cal->scale_y  = (dr_y != 0) ? de_y / dr_y : 1.0f;
    cal->offset_x = (sum_ex / 3.0f) - (sum_rx / 3.0f) * cal->scale_x;
    cal->offset_y = (sum_ey / 3.0f) - (sum_ry / 3.0f) * cal->scale_y;

    ESP_LOGI(TAG, "Cal result: sx=%.4f ox=%.1f sy=%.4f oy=%.1f",
             cal->scale_x, cal->offset_x, cal->scale_y, cal->offset_y);
    return true;
}

void touch_calib_apply(const touch_calib_t *cal)
{
    s_cal = *cal;
    s_cal_loaded = true;
}

void touch_calib_save_to_nvs(const touch_calib_t *cal)
{
    nvs_handle_t h;
    if (nvs_open("lathe", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, "touch_cal", cal, sizeof(*cal));
        nvs_commit(h);
        nvs_close(h);
        ESP_LOGI(TAG, "Kalibracja zapisana w NVS");
    }
}

bool touch_calib_load_from_nvs(touch_calib_t *cal)
{
    nvs_handle_t h;
    if (nvs_open("lathe", NVS_READONLY, &h) == ESP_OK) {
        size_t sz = sizeof(*cal);
        esp_err_t ret = nvs_get_blob(h, "touch_cal", cal, &sz);
        nvs_close(h);
        return (ret == ESP_OK && sz == sizeof(*cal));
    }
    return false;
}

// ── Gesture detection ──────────────────────────────────────
touch_event_t touch_detect_gesture(const touch_raw_t *raw, uint32_t now_ms)
{
    static uint32_t press_start_ms = 0;
    static uint16_t press_x = 0, press_y = 0;
    static bool was_pressed = false;

    bool pressed = (raw->z > 0);

    if (pressed && !was_pressed) {
        // Press start
        press_start_ms = now_ms;
        press_x = raw->x;
        press_y = raw->y;
        was_pressed = true;
        return TOUCH_EVT_PRESS;
    }

    if (!pressed && was_pressed) {
        // Release
        was_pressed = false;
        uint32_t duration = now_ms - press_start_ms;
        int32_t dx = (int32_t)raw->x - (int32_t)press_x;
        int32_t dy = (int32_t)raw->y - (int32_t)press_y;

        if (duration > 500 && abs(dx) < 20 && abs(dy) < 20) {
            return TOUCH_EVT_HOLD;
        }
        if (abs(dx) > 60 && abs(dx) > abs(dy)) {
            return (dx > 0) ? TOUCH_EVT_SWIPE_RIGHT : TOUCH_EVT_SWIPE_LEFT;
        }
        if (abs(dy) > 60 && abs(dy) > abs(dx)) {
            return (dy > 0) ? TOUCH_EVT_SWIPE_DOWN : TOUCH_EVT_SWIPE_UP;
        }
        return TOUCH_EVT_RELEASE;
    }

    return TOUCH_EVT_NONE;
}
