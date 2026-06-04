// ============================================================
//  screen_dro.inc  -  Ekran DRO (Digital Read Out)
//  Dołączany do ui_menu.c przez #include
//
//  ILI9488 po SPI 18-bit jest kosztowny przy pełnym odświeżaniu:
//  480×320×3 B = ~460 kB na klatkę.  Ten ekran rysuje pełny layout
//  tylko przy wejściu, a później wysyła do TFT wyłącznie zmienione pola.
// ============================================================

#define DRO_BG        COLOR_BLACK
#define DRO_Z_COLOR   COLOR_YELLOW
#define DRO_X_COLOR   COLOR_CYAN

// ── Stałe layoutu (pixel-perfect dla 480×320) ───────────────
#define DRO_SB_H    38     // status bar
#define DRO_SEP_H    3     // separator między osiami
#define DRO_FT_H    30     // stopka
// wysokość pasa każdej osi: (320 - 38 - 1 - 3 - 2 - 30) / 2 = 123
#define DRO_ROW_H   ((SCR_H - DRO_SB_H - 1 - DRO_SEP_H - 2 - DRO_FT_H) / 2)
#define DRO_Z_Y     (DRO_SB_H + 1)             // 39
#define DRO_SEP_Y   (DRO_Z_Y  + DRO_ROW_H)     // 162
#define DRO_X_Y     (DRO_SEP_Y + DRO_SEP_H)    // 165
#define DRO_FT_Y    (SCR_H - DRO_FT_H)         // 290

// Pionowe odstępy wewnątrz wiersza osi
#define DRO_VAL_PAD  8
#define DRO_LBL_X   12
#define DRO_VAL_MAR 10

#define DRO_VAL_X      (DRO_LBL_X + DRO_LABEL_W + 8)
#define DRO_VAL_W      (SCR_W - DRO_VAL_X - DRO_VAL_MAR)
#define DRO_UNIT_W     (8 * FONT_MD_W)
#define DRO_UNIT_X     (SCR_W - DRO_UNIT_W - DRO_VAL_MAR)
#define DRO_LABEL_W    (FONT_XL_W + 8)

typedef struct {
    char status[24];
    char z_val[24];
    char z_unit[16];
    char x_val[24];
    char x_unit[16];
    char footer[56];
    uint16_t status_bg;
    uint16_t status_fg;
    uint16_t z_color;
    uint16_t z_unit_color;
    uint16_t x_color;
    uint16_t x_unit_color;
    bool x_available;
} dro_cache_t;

static struct {
    uint8_t     brightness;
    bool        dirty;
    bool        layout_valid;
    dro_cache_t cache;
} dro_ui = { .brightness = 90, .dirty = false, .layout_valid = false };

static void dro_screen_invalidate(void)
{
    dro_ui.layout_valid = false;
    memset(&dro_ui.cache, 0, sizeof(dro_ui.cache));
}

static bool dro_text_changed(char *dst, size_t dst_sz, const char *src)
{
    if (strncmp(dst, src, dst_sz) == 0) return false;
    strncpy(dst, src, dst_sz - 1);
    dst[dst_sz - 1] = '\0';
    return true;
}

static void dro_draw_text_rect(int x, int y, int w, int h,
                               const char *text, uint8_t font,
                               uint16_t fg, uint16_t bg,
                               bool right_align, bool do_flush)
{
    display_fill_rect(x, y, w, h, bg);
    int tx = x;
    if (right_align) {
        int tw = display_text_width(text, font);
        tx = x + w - tw;
        if (tx < x) tx = x;
    }
    display_string(tx, y, text, font, fg, bg);
    if (do_flush) display_flush_rect(x, y, w, h);
}

static void dro_format_value(float mm_val, char *out, size_t out_sz)
{
    int i = (int)mm_val;
    int f = (int)(fabsf(mm_val - (float)i) * 100.0f);
    snprintf(out, out_sz, "%+6d.%02d", i, f);
}

static void dro_draw_status(const char *text, uint16_t fg, uint16_t bg,
                            bool force, bool do_flush)
{
    bool changed = force;
    changed |= dro_text_changed(dro_ui.cache.status,
                                sizeof(dro_ui.cache.status), text);
    changed |= (dro_ui.cache.status_fg != fg);
    changed |= (dro_ui.cache.status_bg != bg);
    if (!changed) return;

    dro_ui.cache.status_fg = fg;
    dro_ui.cache.status_bg = bg;
    display_fill_rect(0, 0, SCR_W, DRO_SB_H, bg);
    display_hline(0, DRO_SB_H, SCR_W, rgb565(55, 55, 55));
    int tw = display_text_width(text, FONT_LG);
    int ty = (DRO_SB_H - FONT_LG_H) / 2;
    display_string((SCR_W - tw) / 2, ty, text, FONT_LG, fg, bg);
    if (do_flush) display_flush_rect(0, 0, SCR_W, DRO_SB_H + 1);
}

static void dro_draw_footer(const char *text, bool force, bool do_flush)
{
    bool changed = force;
    changed |= dro_text_changed(dro_ui.cache.footer,
                                sizeof(dro_ui.cache.footer), text);
    if (!changed) return;

    display_fill_rect(0, DRO_FT_Y, SCR_W, DRO_FT_H, rgb565(14, 14, 14));
    display_hline(0, DRO_FT_Y, SCR_W, rgb565(48, 48, 48));
    int ty = DRO_FT_Y + (DRO_FT_H - FONT_MD_H) / 2;
    display_string(UI_PAD_X, ty, text, FONT_MD, COL_LABEL, rgb565(14, 14, 14));
    if (do_flush) display_flush_rect(0, DRO_FT_Y, SCR_W, DRO_FT_H);
}

static void dro_draw_axis_value(int zone_y, const char *label, const char *value,
                                const char *unit, bool is_x_axis,
                                uint16_t val_color, uint16_t unit_color,
                                bool force, bool do_flush)
{
    dro_cache_t *c = &dro_ui.cache;
    int val_y = zone_y + DRO_VAL_PAD;
    int lbl_y = val_y + (FONT_XXL_H - FONT_XL_H) / 2;
    int unit_y = val_y + FONT_XXL_H + 4;

    char *cached_val = is_x_axis ? c->x_val : c->z_val;
    char *cached_unit = is_x_axis ? c->x_unit : c->z_unit;
    uint16_t *cached_color = is_x_axis ? &c->x_color : &c->z_color;
    uint16_t *cached_unit_color = is_x_axis ? &c->x_unit_color : &c->z_unit_color;
    size_t val_sz = is_x_axis ? sizeof(c->x_val) : sizeof(c->z_val);
    size_t unit_sz = is_x_axis ? sizeof(c->x_unit) : sizeof(c->z_unit);

    bool row_force = force || (is_x_axis && !c->x_available);
    if (row_force) {
        display_fill_rect(0, zone_y, SCR_W, DRO_ROW_H, DRO_BG);
    }

    if (row_force || *cached_color != val_color) {
        display_fill_rect(DRO_LBL_X, lbl_y, DRO_LABEL_W, FONT_XL_H, DRO_BG);
        display_string(DRO_LBL_X, lbl_y, label, FONT_XL, val_color, DRO_BG);
        if (do_flush && !row_force)
            display_flush_rect(DRO_LBL_X, lbl_y, DRO_LABEL_W, FONT_XL_H);
    }

    bool value_changed = row_force;
    value_changed |= dro_text_changed(cached_val, val_sz, value);
    value_changed |= (*cached_color != val_color);
    if (value_changed) {
        dro_draw_text_rect(DRO_VAL_X, val_y, DRO_VAL_W, FONT_XXL_H,
                           value, FONT_XXL, val_color, DRO_BG, true,
                           do_flush && !row_force);
    }

    bool unit_changed = row_force;
    unit_changed |= dro_text_changed(cached_unit, unit_sz, unit);
    unit_changed |= (*cached_unit_color != unit_color);
    if (unit_changed) {
        dro_draw_text_rect(DRO_UNIT_X, unit_y, DRO_UNIT_W, FONT_MD_H,
                           unit, FONT_MD, unit_color, DRO_BG, true,
                           do_flush && !row_force);
    }

    *cached_color = val_color;
    *cached_unit_color = unit_color;
    if (is_x_axis) c->x_available = true;

    if (do_flush && row_force) {
        display_flush_rect(0, zone_y, SCR_W, DRO_ROW_H);
    }
}

static void dro_draw_x_na(bool force, bool do_flush)
{
    bool changed = force || dro_ui.cache.x_available;
    if (!changed) return;

    display_fill_rect(0, DRO_X_Y, SCR_W, DRO_ROW_H, DRO_BG);
    int ny = DRO_X_Y + (DRO_ROW_H - FONT_XL_H) / 2;
    display_string(DRO_LBL_X, ny, "X: N/A", FONT_XL, COL_LABEL, DRO_BG);
    dro_ui.cache.x_available = false;
    dro_ui.cache.x_val[0] = '\0';
    dro_ui.cache.x_unit[0] = '\0';
    if (do_flush) display_flush_rect(0, DRO_X_Y, SCR_W, DRO_ROW_H);
}

// ── draw_dro ────────────────────────────────────────────────
static void draw_dro(void)
{
    spindle_status_t sp;
    spindle_get_status(&sp);

    bool force = !dro_ui.layout_valid;
    if (force) {
        display_fill_rect(0, 0, SCR_W, SCR_H, DRO_BG);
        display_fill_rect(40, DRO_SEP_Y,     SCR_W - 80, 1, rgb565(65, 65, 65));
        display_fill_rect(40, DRO_SEP_Y + 1, SCR_W - 80, 1, rgb565(28, 28, 28));
    }

    char status[24];
    if (sp.estop_active) {
        snprintf(status, sizeof(status), "!!!  E-STOP  !!!");
    } else if (sp.state == SPINDLE_STATE_RUNNING) {
        snprintf(status, sizeof(status), "RPM: %d  %s",
                 sp.rpm_actual,
                 (sp.dir == SPINDLE_DIR_FWD) ? "FWD" : "REV");
    } else {
        snprintf(status, sizeof(status), g_homed ? "STOP  |  HOMED" : "NIE BAZOWANO");
    }

    uint16_t sb_bg = sp.estop_active ? rgb565(185,  0,   0)
                   : (!g_homed)      ? rgb565(130, 60,   0)
                   :                    rgb565(  0, 38,  78);
    uint16_t sb_fg = sp.estop_active ? COL_WHITE
                   : (!g_homed)      ? COL_WARN
                   :                   COL_LIGHT_GREY;
    dro_draw_status(status, sb_fg, sb_bg, force, !force);

    char val[24];
    char unit[16];
    bool zh = limits_axis_homed(AXIS_Z);
    dro_format_value(stepper_get_position_mm(), val, sizeof(val));
    snprintf(unit, sizeof(unit), "mm  %s", zh ? "[OK]" : "[--]");
    dro_draw_axis_value(DRO_Z_Y, "Z", val, unit, false,
                        zh ? DRO_Z_COLOR : COL_WARN,
                        zh ? COL_OK : COL_WARN,
                        force, !force);

    if (g_axis_x) {
        bool xh = limits_axis_homed(AXIS_X);
        dro_format_value(axis_get_position_mm(g_axis_x), val, sizeof(val));
        snprintf(unit, sizeof(unit), "mm  %s", xh ? "[OK]" : "[--]");
        dro_draw_axis_value(DRO_X_Y, "X", val, unit, true,
                            xh ? DRO_X_COLOR : COL_WARN,
                            xh ? COL_OK : COL_WARN,
                            force, !force);
    } else {
        dro_draw_x_na(force, !force);
    }

    char footer[56];
    if (dro_ui.dirty) {
        snprintf(footer, sizeof(footer), "Jasnosc: %d%%", dro_ui.brightness);
    } else {
        snprintf(footer, sizeof(footer), "ENC=jasn  SW=menu  BTN3=0Z  BTN3>>>=0X");
    }
    dro_draw_footer(footer, force, !force);

    if (force) {
        display_flush();
        dro_ui.layout_valid = true;
    }
}

// ── handle_dro (bez zmian) ───────────────────────────────────
static void handle_dro(encoder_event_t evt)
{
    if (evt == ENCODER_EVT_BTN1_LONG) {
        spindle_emergency_stop();
        stepper_stop();
        if (g_axis_x) axis_stop(g_axis_x);
        buzzer_signal_estop();
        ui_menu_notify("!!! E-STOP !!!", COLOR_RED, 3000);
        ui_menu_goto(SCREEN_MAIN);
        return;
    }

    switch (evt) {
    case ENCODER_EVT_CW:
        if (dro_ui.brightness <= 95)
            dro_ui.brightness += 5;
        else
            dro_ui.brightness = 100;
        display_brightness(dro_ui.brightness);
        dro_ui.dirty = true;
        break;
    case ENCODER_EVT_CCW:
        if (dro_ui.brightness >= 10)
            dro_ui.brightness -= 5;
        else
            dro_ui.brightness = 5;
        display_brightness(dro_ui.brightness);
        dro_ui.dirty = true;
        break;
    case ENCODER_EVT_SW_PRESS:
    case ENCODER_EVT_SW_LONG:
    case ENCODER_EVT_BTN2_PRESS:
        ui_menu_goto(SCREEN_MAIN);
        break;
    case ENCODER_EVT_BTN3_PRESS:
        stepper_reset_position();
        ui_menu_notify("Z = 0", COL_OK, 800);
        break;
    case ENCODER_EVT_BTN3_LONG:
        if (g_axis_x) {
            axis_reset_position(g_axis_x);
            ui_menu_notify("X = 0", COL_OK, 800);
        }
        break;
    default:
        break;
    }
}
