// ============================================================
//  display.c  –  Wrapper nad biblioteką nopnop2002/ili9340
//  Mini Lathe Controller v4  /  ESP-IDF 5.x
// ============================================================

#include "display.h"
#include <stdio.h>
#include <string.h>
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <sys/stat.h>
#include "esp_spiffs.h"

static const char *TAG = "DISPLAY";

TFT_t     g_dev;
FontxFile g_font[7][2];

static const char *FONT_PATHS[7] = {
    "/spiffs/ILGH16XB.FNT",
    "/spiffs/ILGH24XB.FNT",
    "/spiffs/ILGH32XB.FNT",
    "/spiffs/ILMH16XB.FNT",
    "/spiffs/ILMH24XB.FNT",
    "/spiffs/ILMH32XB.FNT",
    "/spiffs/LATIN32B.FNT",
};

#define BL_LEDC_TIMER   LEDC_TIMER_0
#define BL_LEDC_CHANNEL LEDC_CHANNEL_0
#define BL_LEDC_FREQ    5000
#define BL_BRIGHTNESS   90

static void _backlight_init(void)
{
    if (DISP_BL < 0) {
        ESP_LOGI(TAG, "Backlight GPIO disabled");
        return;
    }

    ledc_timer_config_t t = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = BL_LEDC_TIMER,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .freq_hz         = BL_LEDC_FREQ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&t);
    ledc_channel_config_t ch = {
        .gpio_num   = DISP_BL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = BL_LEDC_CHANNEL,
        .timer_sel  = BL_LEDC_TIMER,
        .duty       = (BL_BRIGHTNESS * 255) / 100,
        .hpoint     = 0,
    };
    ledc_channel_config(&ch);
}

static esp_err_t _spiffs_init(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path              = "/spiffs",
        .partition_label        = NULL,
        .max_files              = 8,
        // format_if_mount_failed=true: naprawi uszkodzoną partycję
        // UWAGA: formatowanie usuwa dane – fonty zostaną wgrane przy flash
        .format_if_mount_failed = false,
    };
    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err == ESP_OK) {
        size_t total = 0, used = 0;
        esp_spiffs_info(NULL, &total, &used);
        ESP_LOGI(TAG, "SPIFFS OK: %d/%d bajtow uzyto", used, total);
    } else {
        ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t display_init(void)
{
    ESP_LOGI(TAG, "Init TFT model=0x%04x: %dx%d offset=%d/%d",
             DISP_MODEL, DISP_W, DISP_H, DISP_OFFSET_X, DISP_OFFSET_Y);

    // SPIFFS – fonty
    esp_err_t err = _spiffs_init();
    if (err != ESP_OK) return err;

    spi_clock_speed(DISP_SPI_FREQ_HZ);

    // SPI – piny z menuconfig (CONFIG_MOSI_GPIO itd.)
    // MISO=13 dla karty SD na wspólnej magistrali SPI2
    spi_master_init(&g_dev,
        CONFIG_MOSI_GPIO,
        CONFIG_SCLK_GPIO,
        CONFIG_TFT_CS_GPIO,
        CONFIG_DC_GPIO,
        CONFIG_RESET_GPIO,
        CONFIG_BL_GPIO,
        13, -1, -1, -1, -1);   // XPT_MISO=13 (MISO SD), reszta XPT wył.

    lcdInit(&g_dev, DISP_MODEL, DISP_W, DISP_H, DISP_OFFSET_X, DISP_OFFSET_Y);
    // Reduce verbose logging from driver and SPI to avoid flooding monitor
    esp_log_level_set("ILI9340", ESP_LOG_WARN);
    esp_log_level_set("spi", ESP_LOG_WARN);

    // Kierunek tekstu normalny
    lcdSetFontDirection(&g_dev, DIRECTION0);

    // Wczytaj fonty
    for (int i = 0; i < 7; i++) {
        InitFontx(g_font[i], (char*)FONT_PATHS[i], "");
        uint8_t buf[FontxGlyphBufSize];
        uint8_t fw, fh;
        if (GetFontx(g_font[i], 'A', buf, &fw, &fh))
            ESP_LOGI(TAG, "Font[%d] OK: %dx%d px", i, fw, fh);
        else
            ESP_LOGE(TAG, "Font[%d] BLAD: %s", i, FONT_PATHS[i]);
    }

    // Podświetlenie przez LEDC
    _backlight_init();

    // Wyczyść ekran
    lcdFillScreen(&g_dev, COL_BLACK);
    lcdDrawFinish(&g_dev);

    ESP_LOGI(TAG, "FrameBuffer: %s",
             lcdIsFrameBuffer(&g_dev) ? "TAK" : "NIE");
    ESP_LOGI(TAG, "Dev: _width=%d _height=%d",
             g_dev._width, g_dev._height);
    return ESP_OK;
}

void display_flush(void)
{
    lcdDrawFinish(&g_dev);
}

void display_clear(uint16_t c)
{
    lcdFillScreen(&g_dev, c);
}

void display_pixel(int x, int y, uint16_t c)
{
    lcdDrawPixel(&g_dev, x, y, c);
}

void display_hline(int x, int y, int w, uint16_t c)
{
    if (w > 0) lcdDrawFillRect(&g_dev, x, y, x+w-1, y, c);
}

void display_vline(int x, int y, int h, uint16_t c)
{
    if (h > 0) lcdDrawFillRect(&g_dev, x, y, x, y+h-1, c);
}

void display_fill_rect(int x, int y, int w, int h, uint16_t c)
{
    if (w > 0 && h > 0)
        lcdDrawFillRect(&g_dev, x, y, x+w-1, y+h-1, c);
}

void display_draw_rect(int x, int y, int w, int h, uint16_t c)
{
    if (w > 0 && h > 0)
        lcdDrawRect(&g_dev, x, y, x+w-1, y+h-1, c);
}

void display_string(int x, int y, const char *s,
                    uint8_t font_size, uint16_t fg, uint16_t bg)
{
    if (!s || !*s) return;
    if (font_size > FONT_LATIN) font_size = FONT_SM;

    const uint8_t font_h[] = { FONT_SM_H, FONT_MD_H, FONT_LG_H,
                               FONT_MN_SM_H, FONT_MN_MD_H, FONT_MN_LG_H, 32 };
    if (x < 0 || y < 0 || x >= DISP_W || y >= DISP_H) return;

    // The ili9340 driver treats y as the glyph bottom. The public display API
    // uses the more convenient top-left origin used by the UI layout code.
    int baseline_y = y + font_h[font_size] - 1;
    if (baseline_y >= DISP_H) baseline_y = DISP_H - 1;

    if (bg != 0xFFFF) lcdSetFontFill(&g_dev, bg);
    else              lcdUnsetFontFill(&g_dev);
    lcdDrawString(&g_dev, g_font[font_size], x, baseline_y, (uint8_t*)s, fg);
    lcdUnsetFontFill(&g_dev);
}

void display_int(int x, int y, int32_t val,
                 uint8_t font_size, uint16_t fg, uint16_t bg)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%ld", (long)val);
    display_string(x, y, buf, font_size, fg, bg);
}

void display_brightness(uint8_t pct)
{
    if (pct > 100) pct = 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BL_LEDC_CHANNEL, (pct*255)/100);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BL_LEDC_CHANNEL);
}

int display_text_width(const char *s, uint8_t font_size)
{
    const int w[] = {FONT_SM_W, FONT_MD_W, FONT_LG_W,
                     FONT_MN_SM_W, FONT_MN_MD_W, FONT_MN_LG_W, 8};
    if (!s || font_size > FONT_LATIN) return 0;
    return strlen(s) * w[font_size];
}

// ── Bitmap drawing from raw RGB565 buffer ──
void display_draw_bitmap(int x, int y, int w, int h, const uint16_t *data)
{
    if (!data || !w || !h || x >= DISP_W || y >= DISP_H) return;
    // Save original stride before any clipping
    int stride = w;
    // Clip left edge
    if (x < 0) { int shift = -x; if (shift >= w) return; data += shift; w -= shift; x = 0; }
    // Clip right edge
    if (x + w > DISP_W) w = DISP_W - x;
    if (w <= 0) return;
    // Clip top edge (use saved stride for correct row offset)
    if (y < 0) { int shift = -y; if (shift >= h) return; data += shift * stride; h -= shift; y = 0; }
    // Clip bottom edge
    if (y + h > DISP_H) h = DISP_H - y;
    if (h <= 0) return;
    // Draw each row with a single multi-pixel call (1 DMA/memcpy per row)
    for (int row = 0; row < h; row++)
        lcdDrawMultiPixels(&g_dev, x, y + row, w, (uint16_t *)&data[row * stride]);
}

// ── Load raw RGB565 bitmap from SPIFFS and draw ──

void display_draw_bitmap_file(int x, int y, const char *spiffs_path)
{
    if (!spiffs_path) return;

    struct stat st;
    if (stat(spiffs_path, &st) != 0) {
        ESP_LOGE(TAG, "Bitmap file not found: %s", spiffs_path);
        return;
    }

    // Assume file contains raw RGB565 pixels for the full screen (160×128)
    // or a custom size prefixed with 4-byte header: uint16_t w, uint16_t h
    FILE *f = fopen(spiffs_path, "rb");
    if (!f) return;

    uint16_t hdr[2];
    int bmp_w, bmp_h;
    if (fread(hdr, 1, 4, f) == 4 && hdr[0] > 0 && hdr[0] <= DISP_W
                                 && hdr[1] > 0 && hdr[1] <= DISP_H) {
        bmp_w = hdr[0]; bmp_h = hdr[1];
        long file_size = st.st_size;
        int expected = (int)hdr[0] * (int)hdr[1] * 2 + 4; // +4 na nagłówek
        if (expected != file_size)
        {
            // fallback lub odrzuć
        }
    } else {
        // Fallback: full screen (160×128) without header
        bmp_w = DISP_W; bmp_h = DISP_H;
        fseek(f, 0, SEEK_SET);
    }

    int px_count = bmp_w * bmp_h;
    uint16_t *buf = (uint16_t *)heap_caps_malloc(
        px_count * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) { fclose(f); return; }
    if (!buf)
    {
    ESP_LOGE(TAG, "bitmap malloc fail: %d B", px_count * 2);
    fclose(f);
    return;
    }
    int rd = fread(buf, 2, px_count, f);
    fclose(f);

    if (rd > 0) display_draw_bitmap(x, y, bmp_w, bmp_h, buf);
    else ESP_LOGE(TAG, "Failed to read bitmap: %s", spiffs_path);

    free(buf);
}
