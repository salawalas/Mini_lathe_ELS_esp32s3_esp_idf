// screen_touch_calib.inc – Kalibracja dotyku 3-punktowa
// Dołączany do ui_menu.c przez #include

#include "touch.h"

#define CAL_POINTS 3

// Punkty kalibracyjne na ekranie (procentowo: x% * SCR_W, y% * SCR_H)
static const uint16_t CAL_REF_X[CAL_POINTS] = { 10, 90, 50 };  // % szerokości
static const uint16_t CAL_REF_Y[CAL_POINTS] = { 10, 50, 90 };  // % wysokości

static struct {
    uint8_t  step;          // 0..2 = który punkt, 3 = gotowe
    touch_calib_t cal;
    bool     done;
} cal_ui = { .step = 0, .done = false };

static void draw_touch_calib(void)
{
    draw_header("KALIBRACJA DOTYKU", COL_HDR_SET);
    display_fill_rect(0, HEADER_H, SCR_W, CONTENT_H + 2, COLOR_BLACK);

    int16_t y = CONTENT_Y + 4;

    if (cal_ui.step < CAL_POINTS) {
        // Instrukcja
        char buf[48];
        snprintf(buf, sizeof(buf), "Dotknij krzyzyk %d/%d", cal_ui.step + 1, CAL_POINTS);
        int tw = display_text_width(buf, FONT_MD);
        display_draw_string((SCR_W - tw) / 2, y, buf, COLOR_WHITE, COLOR_BLACK, 1);
        y += FONT_MD_H + 10;

        // Narysuj krzyżyk w punkcie kalibracyjnym
        uint16_t cx = SCR_W  * CAL_REF_X[cal_ui.step] / 100;
        uint16_t cy = SCR_H  * CAL_REF_Y[cal_ui.step] / 100;
        uint16_t sz = 16;

        display_draw_hline(cx - sz, cy, sz * 2, COLOR_RED);
        display_vline(cx, cy - sz, sz * 2, COLOR_RED);
        display_draw_rect(cx - sz - 1, cy - sz - 1, sz * 2 + 3, sz * 2 + 3, COLOR_RED);
        display_draw_rect(cx - sz - 2, cy - sz - 2, sz * 2 + 5, sz * 2 + 5, COLOR_RED);

        // Pokaż oczekiwane współrzędne
        snprintf(buf, sizeof(buf), "(%d%%,%d%%)", CAL_REF_X[cal_ui.step], CAL_REF_Y[cal_ui.step]);
        tw = display_text_width(buf, FONT_SM);
        display_draw_string((SCR_W - tw) / 2, cy + sz + 6, buf, COL_LABEL, COLOR_BLACK, 1);

    } else if (cal_ui.done) {
        const char *msg = "Kalibracja zakonczona!";
        int tw = display_text_width(msg, FONT_MD);
        display_draw_string((SCR_W - tw) / 2, y, msg, COL_OK, COLOR_BLACK, 1);
        y += FONT_MD_H + 16;

        char buf[64];
        snprintf(buf, sizeof(buf), "sx=%.4f ox=%.1f", cal_ui.cal.scale_x, cal_ui.cal.offset_x);
        display_draw_string(4, y, buf, COL_VAL, COLOR_BLACK, 1); y += FONT_SM_H + 4;
        snprintf(buf, sizeof(buf), "sy=%.4f oy=%.1f", cal_ui.cal.scale_y, cal_ui.cal.offset_y);
        display_draw_string(4, y, buf, COL_VAL, COLOR_BLACK, 1); y += FONT_SM_H + 10;

        const char *save = "SW=zapisz i wyjdz  BTN2=anuluj";
        tw = display_text_width(save, FONT_SM);
        display_draw_string((SCR_W - tw) / 2, y, save, COL_LABEL, COLOR_BLACK, 1);
    }

    display_draw_hline(4, SCR_H - FOOTER_H - 12, SCR_W - 8, COLOR_LIGHT_GREY);
    display_draw_string(4, SCR_H - FOOTER_H - 6, "BTN2=wroc", COL_LABEL, COLOR_BLACK, 1);

    draw_footer();
    display_flush();
}

static void handle_touch_calib(encoder_event_t evt)
{
    if (evt == ENCODER_EVT_BTN2_PRESS || evt == ENCODER_EVT_SW_LONG) {
        ui_menu_goto(SCREEN_MENU);
        return;
    }

    // Tu obsługa dotyku: odczytaj raw, zapisz jako punkt kalibracyjny
    if (cal_ui.step < CAL_POINTS) {
        touch_raw_t raw;
        if (touch_read_raw(&raw) == ESP_OK && raw.z > 200) {  // próg nacisku
            touch_calib_add_point(&cal_ui.cal, &raw,
                CAL_REF_X[cal_ui.step] * SCR_W / 100,
                CAL_REF_Y[cal_ui.step] * SCR_H / 100);
            cal_ui.step++;
            if (cal_ui.step >= CAL_POINTS) {
                cal_ui.done = touch_calib_compute(&cal_ui.cal);
                ui_menu_notify(cal_ui.done ? "Kalibracja OK!" : "Blad kalibracji",
                              cal_ui.done ? COL_OK : COLOR_RED, 2000);
            }
        }
        return;
    }

    // Kalibracja gotowa — zapisz lub anuluj
    if (cal_ui.done && evt == ENCODER_EVT_SW_PRESS) {
        touch_calib_apply(&cal_ui.cal);
        touch_calib_save_to_nvs(&cal_ui.cal);
        ui_menu_notify("Zapisano w NVS!", COL_OK, 1500);
        ui_menu_goto(SCREEN_MENU);
    }
}
