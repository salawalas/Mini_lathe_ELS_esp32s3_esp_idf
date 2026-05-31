// ============================================================
//  main.c  –  Mini Lathe Controller v6.2.0
//  ST7796 480×320  /  IDF 5.5  /  ESP32-S3
// ============================================================

#define LATHE_VERSION "6.2.0"
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
#include "gcode.h"
#include "buzzer.h"
#include "esp_task_wdt.h"
#include "wifi_server.h"
#include "ble_server.h"
#include "touch.h"
#include <stdio.h>

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
    bool logo_shown = false;

    ESP_LOGI("MAIN", "Szukam /spiffs/logo.raw...");
    struct stat st;
    if (stat("/spiffs/logo.raw", &st) == 0 && st.st_size > 4)
    {
        FILE *f = fopen("/spiffs/logo.raw", "rb");
        if (f)
        {
            uint16_t lw = 0, lh = 0;
            size_t r1 = fread(&lw, sizeof(lw), 1, f);
            size_t r2 = fread(&lh, sizeof(lh), 1, f);
            if (r1 != 1 || r2 != 1)
            {
                ESP_LOGE(TAG, "Nie mozna odczytac rozmiarow logo.raw");
            }
            else
            {
                ESP_LOGI(TAG, "logo.raw: %d x %d px (%ld B)",
                         (int)lw, (int)lh, (long)st.st_size);

                if (lw > 0 && lw <= DISP_W && lh > 0 && lh <= DISP_H)
                {
                    size_t px_count = (size_t)lw * (size_t)lh;
                    size_t bytes = px_count * sizeof(uint16_t);
                    uint16_t *buf = heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
                    if (!buf)
                    {
                        ESP_LOGE(TAG, "malloc fail for %u bytes", (unsigned)bytes);
                    }
                    else
                    {
                        size_t n = fread(buf, sizeof(uint16_t), px_count, f);
                        if (n == px_count)
                        {
                            int logo_x = (DISP_W - (int)lw) / 2;
                            int logo_y = (DISP_H - (int)lh) / 2 - 12;
                            if (logo_y < 0)
                                logo_y = 0;
                            display_draw_bitmap(logo_x, logo_y, lw, lh, buf);
                            logo_shown = true;
                            ESP_LOGI(TAG, "Logo wyswietlone @ %d,%d", logo_x, logo_y);
                        }
                        else
                        {
                            ESP_LOGE(TAG, "Blad odczytu logo: odczytano %u z %u",
                                     (unsigned)n, (unsigned)px_count);
                        }
                        heap_caps_free(buf);
                    } // koniec else (!buf)
                } // koniec if (lw > 0 ...)
                else
                {
                    ESP_LOGW(TAG, "logo.raw: nieprawidlowe wymiary %dx%d", lw, lh);
                } // koniec else (złe wymiary)
            } // koniec else (r1==1 && r2==1)
            fclose(f);
        } // koniec if (f)
    } // koniec if (stat == 0)
    else
    {
        ESP_LOGW("MAIN", "logo.raw nie znaleziony lub za maly");
    }

    if (!logo_shown)
    {
        int mid_y = DISP_H / 2;
        int _w = display_text_width("Mini Lathe", FONT_LG);
        display_string((DISP_W - _w) / 2, mid_y - 32,
                       "Mini Lathe", FONT_LG, COL_WHITE, 0xFFFF);
        int _vw = display_text_width(LATHE_VERSION, FONT_LG);
        display_string((DISP_W - _vw) / 2, mid_y + 4,
                       LATHE_VERSION, FONT_LG, COL_WHITE, 0xFFFF);
    }

    display_fill_rect(0, DISP_H - 24, DISP_W, 24, rgb565(0, 20, 80));
    {
        int _lw = display_text_width(LATHE_NAME, FONT_SM);
        display_string((DISP_W - _lw) / 2, DISP_H - 20,
                       LATHE_NAME, FONT_SM, COL_WHITE, 0xFFFF);
    }
    display_flush();
    vTaskDelay(pdMS_TO_TICKS(5000));
}

// ── 2. Splash screen – 5 sekund ───────────────────────────────
static void show_splash_screen(void)
{
    display_clear(COL_BLACK);
    display_fill_rect(0, 0, DISP_W, SPLASH_HEADER_H, rgb565(0, 80, 160));
    {
        int _tw = display_text_width(LATHE_NAME, FONT_SM);
        display_string((DISP_W - _tw) / 2, SPLASH_TEXT_Y,
                       LATHE_NAME, FONT_SM, COL_WHITE, 0xFFFF);
    }

    display_string(8, SPLASH_L1, "ESP32-S3 / IDF 5.5", FONT_SM, COL_LIGHT_GREY, COL_BLACK);
    display_string(8, SPLASH_L2, "3x DM556 + NEMA23", FONT_SM, COL_GREEN, COL_BLACK);
    display_string(8, SPLASH_L3, "ELS + E-STOP", FONT_SM, COL_CYAN, COL_BLACK);
    display_string(8, SPLASH_L4, "Inicjalizacja...", FONT_SM, COL_YELLOW, COL_BLACK);

    display_fill_rect(0, SPLASH_FOOTER_Y, DISP_W, SPLASH_FOOTER_H, rgb565(20, 20, 20));
    {
        int _fw = display_text_width(LATHE_NAME, FONT_SM);
        display_string((DISP_W - _fw) / 2, SPLASH_FOOTER_Y + SPLASH_TEXT_Y,
                       LATHE_NAME, FONT_SM, COL_BLUE, 0xFFFF);
    }
    display_flush();
    vTaskDelay(pdMS_TO_TICKS(5000));
}

// ── 3. Ostrzeżenie o braku bazowania – migające 1 Hz, 5 sekund
static void show_homing_warning(void)
{
    const int BLINK_HALF_MS = 500;
    const int TOTAL_MS = 5000;
    int elapsed = 0;
    bool on = true;

    while (elapsed < TOTAL_MS)
    {
        uint16_t bg = on ? rgb565(200, 0, 0) : rgb565(80, 0, 0);
        uint16_t fg = COL_WHITE;

        display_clear(bg);

        // Duży napis – wyśrodkowany
        int mid_y = DISP_H / 2;
        int uw = display_text_width("! UWAGA !", FONT_LG);
        display_string((DISP_W - uw) / 2, mid_y - 36,
                       "! UWAGA !", FONT_LG, fg, 0xFFFF);
        display_string(8, mid_y,
                       "Brak bazowania osi!", FONT_MD, fg, 0xFFFF);
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
    if (display_init() != ESP_OK)
    {
        ESP_LOGE(TAG, "Display init FAIL!");
        return;
    }

    // ── Buzzer ────────────────────────────────────────────────
    buzzer_init();

    // ── Sekwencja startowa ────────────────────────────────────
    show_logo_screen();
    show_splash_screen();
    show_homing_warning();

    // ── NVS ───────────────────────────────────────────────────
    esp_err_t nvs_ret = nvs_flash_init();
    if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_LOGW(TAG, "NVS: erase i reinit...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    else
    {
        ESP_ERROR_CHECK(nvs_ret);
    }
    ESP_LOGI(TAG, "NVS OK");

    // ── Peryferia ─────────────────────────────────────────────
    encoder_init();
    axis_init(AXIS_Z);
    axis_init(AXIS_X);
    spindle_init();
    vTaskDelay(pdMS_TO_TICKS(200));

    // ── Krańcówki (LIMIT SWITCHES) – WYŁĄCZONE do testów ───────
    // Aby włączyć: odkomentuj poniższy limits_init().
    // Wymaga fizycznie podłączonych krańcówek NC do GPIO (patrz limits.h):
    //   Z_MIN=IO8, Z_MAX=IO3, X_MIN=IO46, X_MAX=IO45
    // Po odkomentowaniu: ISR krańcówek zatrzymuje oś, a limits_can_move()
    // blokuje ruch w stronę aktywnego limitu.
    //
    // UWAGA: limits_init() musi być wywołane PRZED els_init() i ui_menu_init(),
    //        ponieważ ELS/ui używają limits_can_move().
    // ── Wywołanie (zakomentowane – odkomentuj po podłączeniu HW) ─
    // limits_init();
    //
    // ── Bazowanie (HOMING) – WYŁĄCZONE, dostępne przez UI ────────
    // Aby uruchomić prawdziwy homing:
    //   1. Odkomentuj limits_init() powyżej (linia ~212)
    //   2. W components/ui_menu/screen_homing.inc odkomentuj linie ~157-159:
    //        limits_home_axis(g_axis_z, LIMIT_Z_MIN, AXIS_DIR_NEG, 100.0f);
    //        limits_home_axis(g_axis_x, LIMIT_X_MIN, AXIS_DIR_NEG, 40.0f);
    //   3. Skompiluj i flashuj.
    // Obecnie: Menu → Bazowanie osi → SW (długi) uruchamia tylko symulację czasową.

    // ── ELS + UI – g_homed=false, krzyżyk będzie widoczny ────
    els_init();
    ui_menu_init(); // startuje ui_task, pokazuje dashboard

    // ── SD card – nieblokująca ────────────────────────────────
    if (sdcard_init() == ESP_OK)
    {
        ESP_LOGI(TAG, "Karta SD zamontowana.");
    }
    else
    {
        ESP_LOGW(TAG, "Karta SD nieobecna – dzialanie bez G-code.");
    }

    // ── G-code parser ────────────────────────────────────────
    gcode_init();
    ESP_LOGI(TAG, "Parser G-code gotowy.");

    // ── Touch screen ──────────────────────────────────────
#if CONFIG_TOUCH_TYPE_XPT2046 || CONFIG_TOUCH_TYPE_FT6X06
    {
        esp_err_t ret = touch_init();
        if (ret == ESP_OK)
            ESP_LOGI(TAG, "Touch init OK");
        else
            ESP_LOGW(TAG, "Touch init skipped: %s", esp_err_to_name(ret));
    }
#endif

    // ── WiFi / BLE – warunkowo przez NVS (Ustawienia) ─────
    {
        uint8_t wifi_en = 1, ble_en = 0;
        nvs_handle_t h;
        if (nvs_open("lathe", NVS_READONLY, &h) == ESP_OK) {
            float f = 1.0f; size_t sz = sizeof(f);
            if (nvs_get_blob(h, "wifi_en", &f, &sz) == ESP_OK) wifi_en = (f > 0.5f) ? 1 : 0;
            f = 0.0f; sz = sizeof(f);
            if (nvs_get_blob(h, "ble_en", &f, &sz) == ESP_OK) ble_en = (f > 0.5f) ? 1 : 0;
            nvs_close(h);
        }

        if (wifi_en) {
            esp_err_t ret = wifi_server_init();
            if (ret == ESP_OK)
                ESP_LOGI(TAG, "WiFi/HTTP OK → http://192.168.4.1");
            else
                ESP_LOGW(TAG, "WiFi init failed: %s", esp_err_to_name(ret));
        } else {
            ESP_LOGI(TAG, "WiFi wylaczone (NVS wifi_en=0)");
        }

        if (ble_en) {
            esp_err_t ret = ble_server_init();
            if (ret == ESP_OK)
                ESP_LOGI(TAG, "BLE OK → 'MiniLathe'");
            else
                ESP_LOGW(TAG, "BLE init failed: %s", esp_err_to_name(ret));
        } else {
            ESP_LOGI(TAG, "BLE wylaczone (NVS ble_en=0)");
        }
    }

    // ── Task Watchdog ──────────────────────────────────────
    // TWDT już zainicjalizowany przez ESP-IDF (CONFIG_ESP_TASK_WDT_EN=y).
    // Subskrybujemy tylko główny task — timeout domyślnie 5s, panic.
    esp_task_wdt_add(NULL);
    ESP_LOGI(TAG, "TWDT: main task subscribed");

    ESP_LOGI(TAG, "=== Wszystkie moduly aktywne (g_homed=%d) ===",
             (int)g_homed);

    while (1)
    {
        // Resetuj watchdoga co 2.5s (timeout=5s), loguj co 10s
        for (int i = 0; i < 4; i++) {
            esp_task_wdt_reset();
            vTaskDelay(pdMS_TO_TICKS(2500));
        }
        ESP_LOGD(TAG, "Heap: %lu B  homed: %d",
                 (unsigned long)esp_get_free_heap_size(), (int)g_homed);
    }
}
