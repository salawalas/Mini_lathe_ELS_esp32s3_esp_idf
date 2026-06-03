// ============================================================
//  screen_dro.inc  –  Ekran DRO (Digital Read Out)
//  Dołączany do ui_menu.c przez #include
//
//  Zoptymalizowany dla 480×320 ILI9488 (landscape)
//  ┌──────────────────────────────────── 480px ──────────────────────────────────────┐
//  │  STATUS BAR  h=38  │ RPM: 123 FWD │  FONT_LG (16×32)  kolorowe tło             │
//  ├─────────────────────────────────────────────────────────────────────────────────┤
//  │  Z  (FONT_XL 24×48)        +  123.45  (FONT_XXL 40×64, prawostronnie)   h=123  │
//  │                             mm  [OK]   (FONT_MD 12×24, prawostronnie)           │
//  ├─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ separator ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ h=3  ─ ─┤
//  │  X  (FONT_XL 24×48)        +   45.23  (FONT_XXL 40×64, prawostronnie)   h=123  │
//  │                             mm  [--]   (FONT_MD 12×24, prawostronnie)           │
//  ├─────────────────────────────────────────────────────────────────────────────────┤
//  │  STOPKA  h=30  │ ENC=jasn  SW=menu  BTN3=0Z  BTN3>>>=0X │  FONT_MD             │
//  └─────────────────────────────────────────────────────────────────────────────────┘
//  Łącznie: 38+1+123+3+123+2+30 = 320px ✓
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
#define DRO_VAL_PAD  8     // od góry wiersza do górnej krawędzi FONT_XXL
#define DRO_LBL_X   12     // x etykiety Z / X
#define DRO_VAL_MAR 10     // prawy margines wartości i jednostki

static struct {
    uint8_t brightness;
    bool    dirty;
} dro_ui = { .brightness = 90, .dirty = false };

// ── draw_dro ────────────────────────────────────────────────
static void draw_dro(void)
{
    display_fill_rect(0, 0, SCR_W, SCR_H, DRO_BG);

    spindle_status_t sp;
    spindle_get_status(&sp);

    // ── Pasek statusu (RPM / E-STOP / bazowanie) ────────────
    uint16_t sb_bg = sp.estop_active ? rgb565(185,  0,   0)
                   : (!g_homed)      ? rgb565(130, 60,   0)
                   :                    rgb565(  0, 38,  78);
    display_fill_rect(0, 0, SCR_W, DRO_SB_H, sb_bg);
    display_hline(0, DRO_SB_H, SCR_W, rgb565(55, 55, 55));

    char buf[24];
    if (sp.estop_active) {
        snprintf(buf, sizeof(buf), "!!!  E-STOP  !!!");
    } else if (sp.state == SPINDLE_STATE_RUNNING) {
        snprintf(buf, sizeof(buf), "RPM: %d  %s",
                 sp.rpm_actual,
                 (sp.dir == SPINDLE_DIR_FWD) ? "FWD" : "REV");
    } else {
        snprintf(buf, sizeof(buf), g_homed ? "STOP  |  HOMED" : "NIE BAZOWANO");
    }
    {
        // FONT_LG (16×32) — mieści się w SB_H=38 z 3px marginesem góra/dół
        int tw = display_text_width(buf, FONT_LG);
        int ty = (DRO_SB_H - FONT_LG_H) / 2;
        uint16_t tc = sp.estop_active ? COL_WHITE
                    : (!g_homed)      ? COL_WARN
                    :                   COL_LIGHT_GREY;
        display_string((SCR_W - tw) / 2, ty, buf, FONT_LG, tc, sb_bg);
    }

    // ── Makro do rysowania jednej osi ───────────────────────
    // Rysuje: etykietę (FONT_XL), wartość (FONT_XXL), jednostkę+status (FONT_MD)
    // zone_y: absolutna y górna krawędź strefy osi
#define DRAW_AXIS(zone_y, label_char, mm_val, is_homed, val_color)          \
    do {                                                                      \
        int _val_y = (zone_y) + DRO_VAL_PAD;                                 \
        int _lbl_y = _val_y + (FONT_XXL_H - FONT_XL_H) / 2;                 \
        int _unt_y = _val_y + FONT_XXL_H + 4;                                \
        uint16_t _vc = (val_color);                                           \
        /* Etykieta Z lub X */                                                \
        display_string(DRO_LBL_X, _lbl_y, (label_char), FONT_XL, _vc, DRO_BG); \
        /* Wartość pozycji — format "+  123.45" (9 znaków × 40px = 360px) */ \
        {                                                                      \
            float _m = (mm_val);                                              \
            int   _i = (int)_m;                                               \
            int   _f = (int)(fabsf(_m - (float)_i) * 100.0f);                \
            snprintf(buf, sizeof(buf), "%+6d.%02d", _i, _f);                 \
        }                                                                      \
        {                                                                      \
            int _vw = display_text_width(buf, FONT_XXL);                      \
            display_string(SCR_W - _vw - DRO_VAL_MAR, _val_y,                \
                           buf, FONT_XXL, _vc, DRO_BG);                       \
        }                                                                      \
        /* Jednostka + homing status */                                        \
        snprintf(buf, sizeof(buf), "mm  %s", (is_homed) ? "[OK]" : "[--]");  \
        {                                                                      \
            int _uw = display_text_width(buf, FONT_MD);                       \
            display_string(SCR_W - _uw - DRO_VAL_MAR, _unt_y,                \
                           buf, FONT_MD,                                       \
                           (is_homed) ? COL_OK : COL_WARN, DRO_BG);           \
        }                                                                      \
    } while (0)

    // ── Oś Z ────────────────────────────────────────────────
    {
        bool zh = limits_axis_homed(AXIS_Z);
        DRAW_AXIS(DRO_Z_Y,
                  "Z",
                  stepper_get_position_mm(),
                  zh,
                  zh ? DRO_Z_COLOR : COL_WARN);
    }

    // ── Separator ───────────────────────────────────────────
    display_fill_rect(40, DRO_SEP_Y,     SCR_W - 80, 1, rgb565(65, 65, 65));
    display_fill_rect(40, DRO_SEP_Y + 1, SCR_W - 80, 1, rgb565(28, 28, 28));

    // ── Oś X ────────────────────────────────────────────────
    if (g_axis_x) {
        bool xh = limits_axis_homed(AXIS_X);
        DRAW_AXIS(DRO_X_Y,
                  "X",
                  axis_get_position_mm(g_axis_x),
                  xh,
                  xh ? DRO_X_COLOR : COL_WARN);
    } else {
        // Brak osi X – komunikat środkiem strefy
        int ny = DRO_X_Y + (DRO_ROW_H - FONT_XL_H) / 2;
        display_string(DRO_LBL_X, ny, "X: N/A", FONT_XL, COL_LABEL, DRO_BG);
    }

#undef DRAW_AXIS

    // ── Stopka ──────────────────────────────────────────────
    display_fill_rect(0, DRO_FT_Y, SCR_W, DRO_FT_H, rgb565(14, 14, 14));
    display_hline(0, DRO_FT_Y, SCR_W, rgb565(48, 48, 48));

    const char *hint;
    if (dro_ui.dirty) {
        snprintf(buf, sizeof(buf), "Jasnosc: %d%%", dro_ui.brightness);
        hint = buf;
    } else {
        hint = "ENC=jasn  SW=menu  BTN3=0Z  BTN3>>>=0X";
    }
    {
        int ty = DRO_FT_Y + (DRO_FT_H - FONT_MD_H) / 2;
        display_string(UI_PAD_X, ty, hint, FONT_MD, COL_LABEL, rgb565(14, 14, 14));
    }

    display_flush();
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
