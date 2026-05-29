// gcode.c – Parser i executor G-code dla tokarki CNC
// Obsługuje: G0 G1 G4 G20 G21 G90 G91 G92 M3 M4 M5 M30
#include "gcode.h"
#include "sdcard.h"
#include "axis.h"
#include "stepper.h"
#include "spindle.h"
#include "limits.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "GCODE";

// Maksymalny rozmiar pliku G-code w RAM (~48KB)
#define GCODE_BUF_SIZE  49152
#define GCODE_CHUNK     2048   // ile czytać z SD na raz
#define GCODE_MAX_LINE  256    // maks. długość linii

// Domyślny posuw
#define GCODE_DEFAULT_FEED  100.0f  // mm/min
#define GCODE_RAPID_FEED    400.0f  // mm/min dla G0

static struct {
    gcode_state_t   state;
    gcode_units_t   units;
    gcode_distance_t dist;
    gcode_move_t    move_mode;
    float           feed_rate;        // mm/min (po konwersji)
    float           pos_z, pos_x;     // pozycja wg parsera [mm]
    float           spindle_speed;    // zapisane S
    char           *file_buf;
    uint32_t        file_len;
    uint32_t        line_current;
    uint32_t        line_total;
    char            file_name[64];
    char            error_msg[64];
    bool            stop_flag;
    TaskHandle_t    task_handle;
} g = {0};

// ────────────────────────────────────────────────────────────
//  Pomocnicze: parsowanie liczby
// ────────────────────────────────────────────────────────────
static __attribute__((unused)) float parse_float(const char *s, const char **end)
{
    return strtof(s, (char **)end);
}

// Pomijaj białe znaki
static const char *skip_ws(const char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

// Znajdź wartość dla litery (np. "X10.5" → 10.5, przesuwa wskaźnik)
static bool get_word(const char *line, char letter, float *val)
{
    const char *p = line;
    while (*p) {
        if (toupper((unsigned char)*p) == toupper(letter)) {
            p++;
            *val = strtof(p, (char **)&p);
            return true;
        }
        // Przeskocz do następnego słowa (po spacji)
        while (*p && *p != ' ') p++;
        p = skip_ws(p);
    }
    return false;
}

// Policz linie w buforze
static uint32_t count_lines(const char *buf, uint32_t len)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < len; i++)
        if (buf[i] == '\n') n++;
    if (len > 0 && buf[len - 1] != '\n') n++;
    return n;
}

// Znajdź początek linii nr N (1-based) w buforze
static const char *find_line(const char *buf, uint32_t len, uint32_t n)
{
    uint32_t ln = 1;
    const char *start = buf;
    for (uint32_t i = 0; i < len; i++) {
        if (buf[i] == '\n') {
            if (ln == n) return start;
            ln++;
            start = buf + i + 1;
        }
    }
    if (ln == n) return start;
    return NULL;
}

// Długość linii (do \n lub końca bufora)
static uint32_t line_len(const char *line, const char *buf_end)
{
    const char *p = line;
    while (p < buf_end && *p != '\n' && *p != '\r') p++;
    return (uint32_t)(p - line);
}

// ────────────────────────────────────────────────────────────
//  Konwersja jednostek
// ────────────────────────────────────────────────────────────
static float to_mm(float val)
{
    return (g.units == GCODE_UNITS_INCH) ? val * 25.4f : val;
}

static float feed_to_mm_min(float val)
{
    float f = (g.units == GCODE_UNITS_INCH) ? val * 25.4f : val;
    if (f < 1.0f) f = 1.0f;
    if (f > 500.0f) f = 500.0f;
    return f;
}

// ────────────────────────────────────────────────────────────
//  Wykonanie ruchu
// ────────────────────────────────────────────────────────────
static bool exec_move(float z, float x, float feed)
{
    float target_z = z, target_x = x;

    if (g.dist == GCODE_DISTANCE_ABSOLUTE) {
        // target_z i target_x są już absolutne – nic więcej
    } else {
        // G91: wartości są przyrostowe
        target_z = g.pos_z + z;
        target_x = g.pos_x + x;
    }

    // Sprawdź krańcówki
    if (!limits_can_move(g_axis_z, (target_z >= g.pos_z) ? AXIS_DIR_POS : AXIS_DIR_NEG)) {
        snprintf(g.error_msg, sizeof(g.error_msg), "Krancowka Z blokuje ruch");
        return false;
    }
    if (g_axis_x && !limits_can_move(g_axis_x, (target_x >= g.pos_x) ? AXIS_DIR_POS : AXIS_DIR_NEG)) {
        snprintf(g.error_msg, sizeof(g.error_msg), "Krancowka X blokuje ruch");
        return false;
    }

    float dz = fabsf(target_z - g.pos_z);
    float dx = g_axis_x ? fabsf(target_x - g.pos_x) : 0.0f;

    if (dz < 0.001f && dx < 0.001f) {
        g.pos_z = target_z;
        g.pos_x = target_x;
        return true;
    }

    ESP_LOGI(TAG, "Ruch: Z%.3f→%.3f  X%.3f→%.3f  F%.0f",
             g.pos_z, target_z, g.pos_x, target_x, feed);

    bool z_ok = true, x_ok = true;

    // Ruch Z
    if (dz > 0.001f) {
        z_ok = axis_move_to_mm(g_axis_z, target_z, feed);
    }

    // Ruch X (osobno – tokarka zwykle nie interpoluje)
    if (dx > 0.001f && g_axis_x) {
        x_ok = axis_move_to_mm(g_axis_x, target_x, feed > 0 ? feed : 30.0f);
    }

    if (!z_ok || !x_ok) {
        snprintf(g.error_msg, sizeof(g.error_msg), "Blad ruchu osi");
        return false;
    }

    // Synchronizuj pozycję parsera z hardwarem
    g.pos_z = axis_get_position_mm(g_axis_z);
    if (g_axis_x) g.pos_x = axis_get_position_mm(g_axis_x);
    return true;
}

// ────────────────────────────────────────────────────────────
//  Parsowanie i wykonanie jednej linii G-code
// ────────────────────────────────────────────────────────────
static bool exec_one_line(const char *line_raw, uint32_t len)
{
    // Skopiuj do bufora, usuń komentarz (; lub (...))
    char lbuf[GCODE_MAX_LINE];
    uint32_t j = 0;
    bool in_paren = false;
    for (uint32_t i = 0; i < len && j < sizeof(lbuf) - 1; i++) {
        char c = line_raw[i];
        if (c == ';') break;
        if (c == '(') { in_paren = true; continue; }
        if (c == ')') { in_paren = false; continue; }
        if (in_paren) continue;
        if (c == '\r') continue;
        lbuf[j++] = c;
    }
    lbuf[j] = '\0';

    // Pomijaj puste linie i numery linii (N...)
    const char *p = skip_ws(lbuf);
    if (!*p) return true;  // pusta linia – OK
    if (*p == 'N' || *p == 'n') {
        // Pomijaj numer linii
        while (*p && *p != ' ') p++;
        p = skip_ws(p);
        if (!*p) return true;
    }

    ESP_LOGD(TAG, "Exec: %s", p);

    // Sprawdź czy to G-code
    if (*p == 'G' || *p == 'g') {
        p++;
        int gcode = (int)strtol(p, (char **)&p, 10);

        switch (gcode) {
        case 0:  // G0 – ruch szybki
        case 1:  // G1 – ruch roboczy
        {
            g.move_mode = (gcode == 0) ? GCODE_MOVE_RAPID : GCODE_MOVE_LINEAR;

            float z_val = 0, x_val = 0;
            bool has_z = get_word(lbuf, 'Z', &z_val);
            bool has_x = get_word(lbuf, 'X', &x_val);

            float f_val = 0;
            bool has_f = get_word(lbuf, 'F', &f_val);
            if (has_f && gcode == 1) g.feed_rate = feed_to_mm_min(f_val);

            float feed = (gcode == 0) ? GCODE_RAPID_FEED : g.feed_rate;

            float target_z = g.pos_z, target_x = g.pos_x;
            if (has_z) target_z = to_mm(z_val);
            if (has_x) target_x = to_mm(x_val);

            if (!exec_move(target_z, target_x, feed)) return false;
            break;
        }

        case 4:  // G4 – dwell (P = ms)
        {
            float p_val = 0;
            if (get_word(lbuf, 'P', &p_val)) {
                vTaskDelay(pdMS_TO_TICKS((uint32_t)(p_val)));
            }
            break;
        }

        case 20: g.units = GCODE_UNITS_INCH;  ESP_LOGI(TAG, "G20: cale");  break;
        case 21: g.units = GCODE_UNITS_MM;    ESP_LOGI(TAG, "G21: mm");    break;
        case 90: g.dist = GCODE_DISTANCE_ABSOLUTE; break;
        case 91: g.dist = GCODE_DISTANCE_RELATIVE; break;

        case 92:  // G92 – ustaw pozycję
        {
            float z_val, x_val;
            if (get_word(lbuf, 'Z', &z_val)) {
                g.pos_z = to_mm(z_val);
                axis_set_position(g_axis_z, g.pos_z);
            }
            if (get_word(lbuf, 'X', &x_val) && g_axis_x) {
                g.pos_x = to_mm(x_val);
                axis_set_position(g_axis_x, g.pos_x);
            }
            break;
        }

        default:
            ESP_LOGW(TAG, "Nieobslugiwany G%d – pomijam", gcode);
            break;
        }
        // Fall through to check S-word on same line
    }

    // ── M-codes ──
    else if (*p == 'M' || *p == 'm') {
        p++;
        int mcode = (int)strtol(p, (char **)&p, 10);

        switch (mcode) {
        case 3:  // M3 – wrzeciono CW
            if (!spindle_estop_is_active()) {
                spindle_start(g.spindle_speed, SPINDLE_DIR_FWD);
                ESP_LOGI(TAG, "M3: wrzeciono CW %d RPM", g.spindle_speed);
            } else {
                snprintf(g.error_msg, sizeof(g.error_msg), "E-STOP: M3 zablokowane");
                return false;
            }
            break;

        case 4:  // M4 – wrzeciono CCW
            if (!spindle_estop_is_active()) {
                spindle_start(g.spindle_speed, SPINDLE_DIR_REV);
                ESP_LOGI(TAG, "M4: wrzeciono CCW %d RPM", g.spindle_speed);
            } else {
                snprintf(g.error_msg, sizeof(g.error_msg), "E-STOP: M4 zablokowane");
                return false;
            }
            break;

        case 5:  // M5 – stop wrzeciona
            spindle_stop();
            ESP_LOGI(TAG, "M5: wrzeciono STOP");
            break;

        case 30:  // M30 – koniec programu
            spindle_stop();
            ESP_LOGI(TAG, "M30: koniec programu");
            g.state = GCODE_STATE_DONE;
            return false;  // false = stop, ale nie błąd

        default:
            ESP_LOGW(TAG, "Nieobslugiwany M%d – pomijam", mcode);
            break;
        }
        // Fall through to check S-word on same line
    }

    // ── S-word (prędkość wrzeciona) ──
    // Może być samodzielnie: "S1000"
    {
        float s_val = 0;
        if (get_word(lbuf, 'S', &s_val)) {
            g.spindle_speed = (uint16_t)(s_val > 0 ? s_val : 0);
            if (spindle_get_rpm() > 0) {
                spindle_set_rpm(g.spindle_speed);
            }
        }
    }

    return true;
}

// ────────────────────────────────────────────────────────────
//  Task wykonujący plik G-code linia po linii
// ────────────────────────────────────────────────────────────
static void gcode_task(void *arg)
{
    ESP_LOGI(TAG, "Start G-code: %s (%lu linii)", g.file_name, g.line_total);
    g.line_current = 0;
    g.state = GCODE_STATE_RUNNING;

    for (uint32_t ln = 1; ln <= g.line_total; ln++) {
        if (g.stop_flag) break;

        // Pauza?
        while (g.state == GCODE_STATE_PAUSED) {
            vTaskDelay(pdMS_TO_TICKS(100));
            if (g.stop_flag) goto done;
        }

        const char *line_start = find_line(g.file_buf, g.file_len, ln);
        if (!line_start) {
            snprintf(g.error_msg, sizeof(g.error_msg), "Nie znaleziono linii %lu", ln);
            g.state = GCODE_STATE_ERROR;
            goto done;
        }

        uint32_t llen = line_len(line_start, g.file_buf + g.file_len);
        g.line_current = ln;

        bool ok = exec_one_line(line_start, llen);
        if (!ok) {
            if (g.state == GCODE_STATE_DONE) {
                goto done;  // M30 – normalny koniec
            }
            ESP_LOGE(TAG, "Blad w linii %lu: %s", ln, g.error_msg);
            g.state = GCODE_STATE_ERROR;
            goto done;
        }

        // Krótka przerwa między liniami
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    g.state = GCODE_STATE_DONE;

done:
    spindle_stop();
    stepper_stop();
    g.task_handle = NULL;
    ESP_LOGI(TAG, "G-code zakonczony. Stan: %d", g.state);
    vTaskDelete(NULL);
}

// ────────────────────────────────────────────────────────────
//  API publiczne
// ────────────────────────────────────────────────────────────
void gcode_init(void)
{
    memset(&g, 0, sizeof(g));
    g.state      = GCODE_STATE_IDLE;
    g.units      = GCODE_UNITS_MM;
    g.dist       = GCODE_DISTANCE_ABSOLUTE;
    g.move_mode  = GCODE_MOVE_LINEAR;
    g.feed_rate  = GCODE_DEFAULT_FEED;
    ESP_LOGI(TAG, "Parser G-code gotowy");
}

uint32_t gcode_load_file(const char *filename)
{
    if (!sdcard_is_mounted()) {
        ESP_LOGE(TAG, "Brak karty SD");
        return 0;
    }

    if (g.state == GCODE_STATE_RUNNING || g.state == GCODE_STATE_PAUSED) {
        gcode_stop();
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    // Zwolnij stary bufor
    if (g.file_buf) { free(g.file_buf); g.file_buf = NULL; }

    g.file_buf = (char *)malloc(GCODE_BUF_SIZE);
    if (!g.file_buf) {
        ESP_LOGE(TAG, "Brak RAM na bufor G-code");
        return 0;
    }

    int total = sdcard_read_file(filename, g.file_buf, GCODE_BUF_SIZE);
    if (total <= 0) {
        free(g.file_buf); g.file_buf = NULL;
        ESP_LOGE(TAG, "Nie udalo sie wczytac pliku: %s", filename);
        return 0;
    }

    g.file_len   = (uint32_t)total;
    g.line_total = count_lines(g.file_buf, g.file_len);
    strncpy(g.file_name, filename, sizeof(g.file_name) - 1);

    ESP_LOGI(TAG, "Wczytano %s: %lu B, %lu linii", filename, g.file_len, g.line_total);
    return g.line_total;
}

bool gcode_start(void)
{
    if (!g.file_buf || g.line_total == 0) return false;
    if (g.state == GCODE_STATE_RUNNING || g.state == GCODE_STATE_PAUSED) {
        gcode_stop();
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    // Synchronizuj pozycję parsera z hardwarem
    g.pos_z = axis_get_position_mm(g_axis_z);
    if (g_axis_x) g.pos_x = axis_get_position_mm(g_axis_x);

    g.stop_flag    = false;
    g.spindle_speed = 0;
    g.error_msg[0]  = '\0';

    xTaskCreate(gcode_task, "gcode_task", 4096, NULL, 5, &g.task_handle);
    return true;
}

void gcode_stop(void)
{
    if (g.state != GCODE_STATE_RUNNING && g.state != GCODE_STATE_PAUSED) return;
    g.stop_flag = true;
    spindle_stop();
    stepper_stop();
    if (g.task_handle) xTaskNotifyGive(g.task_handle);
    g.state = GCODE_STATE_IDLE;
}

void gcode_pause(void)
{
    if (g.state == GCODE_STATE_RUNNING) {
        g.state = GCODE_STATE_PAUSED;
        stepper_stop();
    }
}

void gcode_resume(void)
{
    if (g.state == GCODE_STATE_PAUSED) g.state = GCODE_STATE_RUNNING;
}

void gcode_get_status(gcode_status_t *st)
{
    memset(st, 0, sizeof(*st));
    st->state        = g.state;
    st->line_current = g.line_current;
    st->line_total   = g.line_total;
    st->pos_z_mm     = g.pos_z;
    st->pos_x_mm     = g.pos_x;
    st->spindle_rpm  = g.spindle_speed;
    st->spindle_on   = spindle_get_rpm() > 0;
    strncpy(st->error_msg, g.error_msg, sizeof(st->error_msg) - 1);
    strncpy(st->file_name, g.file_name, sizeof(st->file_name) - 1);
}

gcode_state_t gcode_get_state(void) { return g.state; }
bool gcode_is_running(void) { return g.state == GCODE_STATE_RUNNING; }

bool gcode_exec_line(const char *line)
{
    if (g.state == GCODE_STATE_RUNNING) return false;

    // Synchronizuj pozycję
    g.pos_z = axis_get_position_mm(g_axis_z);
    if (g_axis_x) g.pos_x = axis_get_position_mm(g_axis_x);

    uint32_t llen = (uint32_t)strlen(line);
    return exec_one_line(line, llen);
}