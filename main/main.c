// ============================================================
//  main.c  –  Mini Lathe Controller v6.1
//  ST7796 480×320  /  IDF 5.5  /  ESP32-S3
// ============================================================

#define LATHE_VERSION "6.1"
#define LATHE_NAME "Mini Lathe v" LATHE_VERSION
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "display.h"
#include "encoder.h"
#include "axis.h"
#include "limits.h"
#include "stepper.h"
#include "spindle.h"
#include "ui_menu.h"
#include "motion.h"
#include "sdcard.h"
#include <sys/stat.h>
#include "homing_state.h"

static const char *TAG = "MAIN";

// ── Stałe splash – skalują się z każdą rozdzielczością ────────
#define SPLASH_HEADER_H (DISP_H / 10)
#define SPLASH_FOOTER_H (DISP_H / 10)
#define SPLASH_FOOTER_Y (DISP_H - SPLASH_FOOTER_H)
#define SPLASH_CONTENT_Y (SPLASH_HEADER_H + DISP_H / 16)
#define SPLASH_CONTENT_H (DISP_H - SPLASH_HEADER_H - SPLASH_FOOTER_H - DISP_H / 8)
#define SPLASH_ROW_H (SPLASH_CONTENT_H / 4)
#define SPLASH_L1 (SPLASH_CONTENT_Y + SPLASH_ROW_H * 0)
#define SPLASH_L2 (SPLASH_CONTENT_Y + SPLASH_ROW_H * 1)
#define SPLASH_L3 (SPLASH_CONTENT_Y + SPLASH_ROW_H * 2)
#define SPLASH_L4 (SPLASH_CONTENT_Y + SPLASH_ROW_H * 3)
#define SPLASH_TITLE_X (DISP_W / 2 - DISP_W / 9)
#define SPLASH_FOOT_X (DISP_W / 2 - DISP_W / 11)
#define SPLASH_TEXT_Y ((SPLASH_HEADER_H - FONT_SM_H) / 2)

// ── 1. Ekran logo – 5 sekund ──────────────────────────────────
static void show_logo_screen(void)
{
    display_clear(rgb565(0, 40, 120));

    struct stat st;
    if (stat("/spiffs/logo.raw", &st) == 0 && st.st_size > 4)
    {
        // Akceptuj plik z nagłówkiem (uint16_t w, uint16_t h) dowolnego rozmiaru
        // Wyśrodkuj na ekranie
        FILE *f = fopen("/spiffs/logo.raw", "rb");
        if (f)
        {
            uint16_t lw = 0, lh = 0;
            if (fread(&lw, 2, 1, f) == 1 && fread(&lh, 2, 1, f) == 1 && lw > 0 && lw <= DISP_W && lh > 0 && lh <= DISP_H)
            {
                int logo_x = (DISP_W - lw) / 2;
                int logo_y = (DISP_H - lh) / 2 - 12; // lekko wyżej, zostawia miejsce na pasek
                int px = lw * lh;
                uint16_t *buf = heap_caps_malloc(px * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                if (buf && fread(buf, 2, px, f) == px)
                    display_draw_bitmap(logo_x, logo_y, lw, lh, buf);
                if (buf)
                    free(buf);
            }
            fclose(f);
        }
    }
    else
    {
        // Fallback tekstowy
        int mid_y = DISP_H / 2;
        display_string(DISP_W / 2 - 56, mid_y - 48,
                       "Mini Lathe", FONT_LG, COL_WHITE, 0xFFFF);
        display_string(DISP_W / 2 - 20, mid_y,
                       LATHE_VERSION, FONT_LG, COL_WHITE, 0xFFFF);
        display_string(DISP_W / 2 - 64, mid_y + 48,
                       "ESP32-S3", FONT_MD, rgb565(160, 210, 255), 0xFFFF);
        uint8_t title_font = (DISP_W >= 240) ? FONT_LG : FONT_SM;
        int title_x = (DISP_W - display_text_width("Mini Lathe", title_font)) / 2;
        display_string(title_x, mid_y - 32, "Mini Lathe", title_font, COL_WHITE, 0xFFFF);
    }

    display_fill_rect(0, DISP_H - 24, DISP_W, 24, rgb565(0, 20, 80));
    display_string(DISP_W / 2 - 44, DISP_H - 20,
                   LATHE_NAME, FONT_SM, COL_WHITE, 0xFFFF);
    display_flush();
    vTaskDelay(pdMS_TO_TICKS(5000));
}

// ── 2. Splash screen – 5 sekund ───────────────────────────────
static void show_splash_screen(void)
{
    display_clear(COL_BLACK);
    display_fill_rect(0, 0, DISP_W, SPLASH_HEADER_H, rgb565(0, 80, 160));
    display_string(SPLASH_TITLE_X, SPLASH_TEXT_Y,
                   LATHE_NAME, FONT_SM, COL_WHITE, 0xFFFF);

    display_string(8, SPLASH_L1, "ESP32-S3 / IDF 5.5",  FONT_SM, COL_LIGHT_GREY, COL_BLACK);
    display_string(8, SPLASH_L2, "3x DM556 + NEMA23",   FONT_SM, COL_GREEN,      COL_BLACK);
    display_string(8, SPLASH_L3, "ELS + E-STOP",         FONT_SM, COL_CYAN,       COL_BLACK);
    display_string(8, SPLASH_L4, "Inicjalizacja...",     FONT_SM, COL_YELLOW,     COL_BLACK);

    display_fill_rect(0, SPLASH_FOOTER_Y, DISP_W, SPLASH_FOOTER_H, rgb565(20, 20, 20));
    display_string(SPLASH_FOOT_X, SPLASH_FOOTER_Y + SPLASH_TEXT_Y,
                   LATHE_NAME, FONT_SM, COL_BLUE, 0xFFFF);
    display_flush();
    vTaskDelay(pdMS_TO_TICKS(5000));
}

// ── 3. Ostrzeżenie o braku bazowania – migające 1 Hz, 5 sekund
static void show_homing_warning(void)
{
    const int BLINK_HALF_MS = 500;
    const int TOTAL_MS      = 5000;
    int elapsed = 0;
    bool on = true;

    while (elapsed < TOTAL_MS) {
        uint16_t bg  = on ? rgb565(200, 0, 0) : rgb565(80, 0, 0);
        uint16_t fg  = COL_WHITE;

        display_clear(bg);

        // Duży napis – wyśrodkowany
        int mid_y = DISP_H / 2;
        display_string(DISP_W / 2 - 72, mid_y - 36,
                       "! UWAGA !",     FONT_LG, fg, 0xFFFF);
        display_string(8, mid_y,
                       "Brak bazowania osi!",  FONT_MD, fg, 0xFFFF);
        display_string(8, mid_y + 30,
                       "Idz do Menu > Bazowanie osi",
                       FONT_SM, rgb565(255, 200, 200), 0xFFFF);
        display_flush();

        vTaskDelay(pdMS_TO_TICKS(BLINK_HALF_MS));
        elapsed += BLINK_HALF_MS;
        on = !on;
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== " LATHE_NAME " START ===");

    // ── Display init (SPIFFS + fonty) ────────────────────────
    if (display_init() != ESP_OK) {
        ESP_LOGE(TAG, "Display init FAIL!");
        return;
    }

    // ── Sekwencja startowa ────────────────────────────────────
    show_logo_screen();
    show_splash_screen();
    show_homing_warning();

    // ── NVS ───────────────────────────────────────────────────
    esp_err_t nvs_ret = nvs_flash_init();
    if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS: erase i reinit...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(nvs_ret);
    }
    ESP_LOGI(TAG, "NVS OK");

    // ── Peryferia ─────────────────────────────────────────────
    encoder_init();
    axis_init(AXIS_Z);
    axis_init(AXIS_X);
    spindle_init();
    vTaskDelay(pdMS_TO_TICKS(200));

    // ── Homing – wyłączony, dostępny z Menu > Bazowanie osi ──
    // limits_init();
    // Bazowanie odbywa się tylko przez: Menu → Bazowanie osi → SW (długi)

    // ── ELS + UI – g_homed=false, krzyżyk będzie widoczny ────
    els_init();
    ui_menu_init();   // startuje ui_task, pokazuje dashboard

    // ── SD card – nieblokująca ────────────────────────────────
    if (sdcard_init() == ESP_OK) {
        ESP_LOGI(TAG, "Karta SD zamontowana.");
    } else {
        ESP_LOGW(TAG, "Karta SD nieobecna – dzialanie bez G-code.");
    }

    ESP_LOGI(TAG, "=== Wszystkie moduly aktywne (g_homed=%d) ===",
             (int)g_homed);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        ESP_LOGD(TAG, "Heap: %lu B  homed: %d",
                 (unsigned long)esp_get_free_heap_size(), (int)g_homed);
    }
}
