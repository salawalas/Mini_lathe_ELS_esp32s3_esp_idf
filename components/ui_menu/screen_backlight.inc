// screen_backlight.inc – Ekran regulacji podświetlenia
// Dołączany do ui_menu.c przez #include
//
// Działanie:
//   ENC CW/CCW  = zmiana jasności na żywo (±5%)
//   SW krótki   = zapisz do NVS i wróć do menu
//   SW długi    = zapisz do NVS i wróć do menu
//   BTN2        = anuluj (przywróć poprzednią wartość) i wróć

#define BL_NVS_KEY   "bl_pct"
#define BL_MIN       5
#define BL_MAX       100
#define BL_STEP      5

static struct {
    uint8_t pct;          // bieżąca wartość (na żywo)
    uint8_t pct_saved;    // wartość przed wejściem (do anulowania)
    bool    dirty;        // czy zmieniono ale nie zapisano
} bl_ui;

static void bl_load_nvs(void)
{
    nvs_handle_t h;
    if (nvs_open("lathe", NVS_READONLY, &h) == ESP_OK) {
        uint8_t v = 90;
        nvs_get_u8(h, BL_NVS_KEY, &v);
        nvs_close(h);
        bl_ui.pct = v;
    } else {
        bl_ui.pct = 90;
    }
}

static void bl_save_nvs(void)
{
    nvs_handle_t h;
    if (nvs_open("lathe", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, BL_NVS_KEY, bl_ui.pct);
        nvs_commit(h);
        nvs_close(h);
        bl_ui.dirty = false;
        ui_menu_notify("Zapisano!", COL_OK, 800);
    } else {
        ui_menu_notify("Blad zapisu NVS", COL_WARN, 1500);
    }
}

static void draw_backlight(void)
{
    draw_header("PODSWIETLENIE", COL_HDR_BACKLIGHT);
    display_fill_rect(0, HEADER_H, SCR_W, CONTENT_H + 2, COLOR_BLACK);

    int16_t y = CONTENT_Y + 4;
    char buf[16];

    // Etykieta
    display_draw_string(UI_PAD_X, y, "Jasnosc ekranu:", COL_LABEL, COLOR_BLACK, 1);
    y += ROW_H + 2;

    // Pasek – tło
    const int BAR_X  = UI_PAD_X;
    const int BAR_W  = SCR_W - UI_PAD_X * 2;
    const int BAR_H  = ROW_H;
    display_fill_rect(BAR_X, y, BAR_W, BAR_H, rgb565(20, 10, 0));
    display_draw_rect(BAR_X, y, BAR_W, BAR_H, rgb565(120, 60, 0));

    // Pasek – wypełnienie (kolor zmienia się od ciemnopomarańczowego do żółtego)
    int fill_w = (BAR_W - 4) * bl_ui.pct / 100;
    if (fill_w > 0) {
        uint8_t r = (uint8_t)(80  + (uint32_t)bl_ui.pct * 175 / 100);
        uint8_t g = (uint8_t)(20  + (uint32_t)bl_ui.pct * 195 / 100);
        display_fill_rect(BAR_X + 2, y + 2, fill_w, BAR_H - 4, rgb565(r, g, 0));
    }

    // Procent wyśrodkowany na pasku
    snprintf(buf, sizeof(buf), "%d%%", (int)bl_ui.pct);
    int txt_x = BAR_X + BAR_W / 2 - display_text_width(buf, FONT_SM) / 2;
    display_draw_string(txt_x, y + UI_TEXT_Y, buf, COL_WHITE, 0xFFFF, 1);
    y += BAR_H + 6;

    // Skala 0% – 100%
    display_draw_string(BAR_X,            y, "0%",   COL_LABEL, COLOR_BLACK, 1);
    display_draw_string(BAR_X + BAR_W - 20, y, "100%", COL_LABEL, COLOR_BLACK, 1);
    y += ROW_H;

    // Wskaźnik niezapisanych zmian
    if (bl_ui.dirty) {
        display_draw_string(UI_PAD_X, y, "* niezapisane", rgb565(200, 120, 0), COLOR_BLACK, 1);
    }

    // Stopka – przycisk ZAPISZ
    display_fill_rect(0, FOOTER_Y, SCR_W, FOOTER_H, rgb565(0, 60, 0));
    display_draw_string(UI_PAD_X, FOOTER_Y + UI_FOOTER_TEXT_Y,
                        "SW=Zapisz  BTN2=Anuluj", COL_WHITE, 0xFFFF, 1);

    display_flush();
}

static void handle_backlight(encoder_event_t evt)
{
    if (evt == ENCODER_EVT_BTN1_LONG) {
        spindle_emergency_stop();
        ui_menu_notify("!!! E-STOP !!!", COLOR_RED, 3000);
        ui_menu_goto(SCREEN_MAIN);
        return;
    }

    switch (evt) {
        case ENCODER_EVT_CW:
            if (bl_ui.pct <= BL_MAX - BL_STEP)
                bl_ui.pct += BL_STEP;
            else
                bl_ui.pct = BL_MAX;
            display_brightness(bl_ui.pct);
            bl_ui.dirty = true;
            break;

        case ENCODER_EVT_CCW:
            if (bl_ui.pct >= BL_MIN + BL_STEP)
                bl_ui.pct -= BL_STEP;
            else
                bl_ui.pct = BL_MIN;
            display_brightness(bl_ui.pct);
            bl_ui.dirty = true;
            break;

        case ENCODER_EVT_SW_PRESS:
        case ENCODER_EVT_SW_LONG:
            bl_save_nvs();
            ui_menu_goto(SCREEN_MENU);
            break;

        case ENCODER_EVT_BTN2_PRESS:
        case ENCODER_EVT_BTN2_LONG:
            // Anuluj – przywróć poprzednią wartość
            bl_ui.pct   = bl_ui.pct_saved;
            bl_ui.dirty = false;
            display_brightness(bl_ui.pct);
            ui_menu_goto(SCREEN_MENU);
            break;

        default:
            break;
    }
}

// Wywołaj przy wejściu na ekran – zapamiętaj bieżącą wartość
static void backlight_screen_enter(void)
{
    bl_load_nvs();
    bl_ui.pct_saved = bl_ui.pct;
    bl_ui.dirty     = false;
}
