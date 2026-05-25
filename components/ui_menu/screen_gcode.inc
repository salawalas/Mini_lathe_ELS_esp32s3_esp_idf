// screen_gcode.inc – Ekran G-code z karty SD
// Dołączany do ui_menu.c przez #include

#define GCODE_FILELIST_MAX 24

static struct {
    char     files[GCODE_FILELIST_MAX][56];
    uint32_t sizes[GCODE_FILELIST_MAX];
    uint8_t  file_count;
    uint8_t  sel;
    bool     loaded;
    gcode_status_t status;
} gcode_ui = {0};

// ── Callback dla sdcard_list_gcode_files ──
static void gcode_file_cb(const char *name, uint32_t size, void *arg)
{
    (void)arg;
    if (gcode_ui.file_count >= GCODE_FILELIST_MAX) return;
    strncpy(gcode_ui.files[gcode_ui.file_count], name,
            sizeof(gcode_ui.files[0]) - 1);
    gcode_ui.files[gcode_ui.file_count]
        [sizeof(gcode_ui.files[0]) - 1] = '\0';
    gcode_ui.sizes[gcode_ui.file_count] = size;
    gcode_ui.file_count++;
}

// ── Odśwież listę plików z karty SD ──
static void gcode_refresh_files(void)
{
    gcode_ui.file_count = 0;
    gcode_ui.sel = 0;
    sdcard_list_gcode_files(gcode_file_cb, NULL);
    gcode_ui.loaded = true;
}

static void draw_gcode(void)
{
    draw_header("G-CODE (SD)", COL_HDR_GCODE);
    display_fill_rect(0, HEADER_H, SCR_W, CONTENT_H+2, COLOR_BLACK);

    char buf[80];
    int16_t y = CONTENT_Y + 2;

    if (!sdcard_is_mounted()) {
        display_draw_string(4, y, "Brak karty SD!", COLOR_RED, COLOR_BLACK, 1);
        draw_footer();
        display_flush();
        return;
    }

    gcode_get_status(&gcode_ui.status);
    gcode_state_t st = gcode_ui.status.state;

    if (st == GCODE_STATE_RUNNING || st == GCODE_STATE_PAUSED) {
        // ── Ekran wykonania ──
        snprintf(buf, sizeof(buf), "Plik: %s", gcode_ui.status.file_name);
        display_draw_string(4, y, buf, COL_VAL, COLOR_BLACK, 1); y += ROW_H;

        snprintf(buf, sizeof(buf), "Linia: %lu / %lu",
                 gcode_ui.status.line_current, gcode_ui.status.line_total);
        display_draw_string(4, y, buf, COL_LABEL, COLOR_BLACK, 1); y += ROW_H;

        int pct = gcode_ui.status.line_total > 0
                  ? (int)(gcode_ui.status.line_current * 100L / gcode_ui.status.line_total)
                  : 0;
        if (pct > 100) pct = 100;

        // Pasek postępu
        const int BAR_X = UI_PAD_X;
        const int BAR_W = SCR_W - UI_PAD_X * 2;
        const int BAR_H = ROW_H;
        display_fill_rect(BAR_X, y, BAR_W, BAR_H, rgb565(10, 30, 10));
        int fill_w = (BAR_W - 4) * pct / 100;
        if (fill_w > 0)
            display_fill_rect(BAR_X + 2, y + 2, fill_w, BAR_H - 4, rgb565(0, 180, 0));
        snprintf(buf, sizeof(buf), "%d%%", pct);
        int tx = BAR_X + BAR_W / 2 - display_text_width(buf, FONT_SM) / 2;
        display_draw_string(tx, y + 2, buf, COL_WHITE, 0xFFFF, 1);
        y += BAR_H + 4;

        snprintf(buf, sizeof(buf), "Z: %.2f  X: %.2f mm",
                 gcode_ui.status.pos_z_mm, gcode_ui.status.pos_x_mm);
        display_draw_string(4, y, buf, COL_CYAN, COLOR_BLACK, 1); y += ROW_H;

        snprintf(buf, sizeof(buf), "RPM: %d  %s",
                 gcode_ui.status.spindle_rpm,
                 gcode_ui.status.spindle_on ? "ON" : "OFF");
        display_draw_string(4, y, buf, COL_LABEL, COLOR_BLACK, 1); y += ROW_H;

        if (st == GCODE_STATE_PAUSED)
            display_draw_string(4, y, ">> PAUZA <<", COLOR_YELLOW, COLOR_BLACK, 1);
        else
            display_draw_string(4, y, "BTN1=stop BTN2=pauza", COL_WARN, COLOR_BLACK, 1);

    } else if (gcode_ui.status.error_msg[0]) {
        // ── Błąd ──
        display_draw_string(4, y, "BLAD:", COLOR_RED, COLOR_BLACK, 1); y += ROW_H;
        display_draw_string(4, y, gcode_ui.status.error_msg, COLOR_ORANGE, COLOR_BLACK, 1);
        y += ROW_H;
        display_draw_string(4, y, "Linia: (patrz log)", COL_LABEL, COLOR_BLACK, 1);

    } else {
        // ── Lista plików ──
        if (!gcode_ui.loaded) gcode_refresh_files();

        if (gcode_ui.file_count == 0) {
            display_draw_string(4, y, "Brak plikow .nc/.g/.gco", COL_LABEL, COLOR_BLACK, 1);
            y += ROW_H;
            display_draw_string(4, y, "na karcie SD.", COL_LABEL, COLOR_BLACK, 1);
        } else {
            // Scroll
            int8_t top = (int8_t)gcode_ui.sel - 3;
            if (top < 0) top = 0;
            int visible = 0;

            for (int i = top; i < (int)gcode_ui.file_count && visible < 6; i++, visible++) {
                bool sel = (i == gcode_ui.sel);
                uint16_t bg = sel ? rgb565(20, 80, 20) : COLOR_BLACK;
                uint16_t fg = sel ? COLOR_WHITE : COL_VAL;

                display_fill_rect(0, y, SCR_W, ROW_H, bg);
                // Nazwa pliku (skróć jeśli za długa)
                const char *fn = gcode_ui.files[i];
                int maxw = (SCR_W - UI_PAD_X - 50);
                char fn_buf[64];
                if (display_text_width(fn, FONT_SM) > maxw) {
                    strncpy(fn_buf, fn, sizeof(fn_buf) - 1);
                    fn_buf[sizeof(fn_buf) - 1] = '\0';
                    int fl = strlen(fn_buf);
                    while (fl > 0 && display_text_width(fn_buf, FONT_SM) > maxw) {
                        fn_buf[--fl] = '\0';
                    }
                    fn = fn_buf;
                }
                display_draw_string(UI_PAD_X, y + UI_TEXT_Y, fn, fg, bg, 1);
                // Rozmiar
                uint32_t sz = gcode_ui.sizes[i];
                if (sz > 1024)
                    snprintf(buf, sizeof(buf), "%luKB", (unsigned long)(sz / 1024));
                else
                    snprintf(buf, sizeof(buf), "%lu B", (unsigned long)sz);
                int sx = SCR_W - display_text_width(buf, FONT_SM) - UI_PAD_X;
                display_draw_string(sx, y + UI_TEXT_Y, buf, COL_LABEL, bg, 1);
                y += ROW_H;
            }

            // Strzałki przewijania
            if (gcode_ui.sel > 3)
                display_draw_string(SCR_W / 2 - 4, CONTENT_Y + 1,
                                    "^", rgb565(80,80,80), 0xFFFF, 1);
            if (gcode_ui.file_count - gcode_ui.sel > 4)
                display_draw_string(SCR_W / 2 - 4, FOOTER_Y - FONT_SM_H - 2,
                                    "v", rgb565(80,80,80), 0xFFFF, 1);
        }
    }

    draw_footer();
    display_flush();
}

static void gcode_start_selected(void)
{
    if (gcode_ui.sel >= gcode_ui.file_count) return;
    const char *fn = gcode_ui.files[gcode_ui.sel];
    uint32_t lines = gcode_load_file(fn);
    if (lines == 0) {
        ui_menu_notify("Blad ladowania!", COLOR_RED, 2000);
        return;
    }
    if (gcode_start()) {
        ui_menu_notify("G-code START", COL_OK, 1500);
    } else {
        ui_menu_notify("Blad startu!", COLOR_RED, 2000);
    }
}

static void handle_gcode(encoder_event_t evt)
{
    gcode_get_status(&gcode_ui.status);
    gcode_state_t st = gcode_ui.status.state;

    // E-STOP
    if (evt == ENCODER_EVT_BTN1_LONG) {
        gcode_stop();
        spindle_emergency_stop();
        stepper_stop();
        buzzer_signal_estop();
        ui_menu_notify("!!! E-STOP !!!", COLOR_RED, 3000);
        ui_menu_goto(SCREEN_MAIN);
        return;
    }

    if (st == GCODE_STATE_RUNNING || st == GCODE_STATE_PAUSED) {
        switch (evt) {
        case ENCODER_EVT_BTN1_PRESS:
            gcode_stop();
            buzzer_signal_warn();
            ui_menu_notify("G-code STOP", COL_WARN, 1000);
            break;
        case ENCODER_EVT_BTN2_PRESS:
            if (st == GCODE_STATE_RUNNING) {
                gcode_pause();
                ui_menu_notify("G-code PAUZA", COLOR_YELLOW, 1000);
            } else {
                gcode_resume();
                ui_menu_notify("G-code WZNOWIONO", COL_OK, 1000);
            }
            break;
        default:
            break;
        }
        return;
    }

    // Stan IDLE / DONE / ERROR — nawigacja po liście
    switch (evt) {
    case ENCODER_EVT_CW:
        if (gcode_ui.file_count > 0)
            gcode_ui.sel = (gcode_ui.sel + 1) % gcode_ui.file_count;
        break;
    case ENCODER_EVT_CCW:
        if (gcode_ui.file_count > 0)
            gcode_ui.sel = (gcode_ui.sel == 0) ? (gcode_ui.file_count - 1) : (gcode_ui.sel - 1);
        break;
    case ENCODER_EVT_SW_PRESS:
        gcode_start_selected();
        break;
    case ENCODER_EVT_BTN3_PRESS:
        gcode_start_selected();
        break;
    case ENCODER_EVT_BTN2_LONG:
    case ENCODER_EVT_SW_LONG:
        ui_menu_goto(SCREEN_MENU);
        break;
    case ENCODER_EVT_BTN1_PRESS:
        // Odśwież listę
        gcode_ui.loaded = false;
        break;
    case ENCODER_EVT_BTN2_PRESS:
        // Reset błędu
        if (gcode_ui.status.error_msg[0]) {
            gcode_ui.status.error_msg[0] = '\0';
            gcode_ui.loaded = false;
        }
        break;
    default:
        break;
    }
}