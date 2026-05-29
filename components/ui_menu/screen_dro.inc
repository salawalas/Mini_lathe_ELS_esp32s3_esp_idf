// screen_dro.inc – Ekran DRO (Digital Read Out) – duże cyfry
// Dołączany do ui_menu.c przez #include
//
// Wyświetla pozycje Z i X maksymalnie dużą czcionką.
// Pod spodem: RPM wrzeciona, status homingu, stan E-STOP.
// ENC = zmiana jasności na żywo, SW = powrót do menu, BTN1 = E-STOP

#define DRO_BG        COLOR_BLACK
#define DRO_Z_COLOR   COLOR_YELLOW
#define DRO_X_COLOR   COLOR_CYAN
#define DRO_RPM_COLOR rgb565(100, 200, 100)

static struct {
    uint8_t brightness;
    bool    dirty;
} dro_ui = { .brightness = 90, .dirty = false };

static void draw_dro(void)
{
    display_fill_rect(0, 0, SCR_W, SCR_H, DRO_BG);

    spindle_status_t sp;
    spindle_get_status(&sp);

    // ── Górny pasek statusu (cienki) ──
    int status_h = FONT_SM_H + 4;
    uint16_t status_bg = sp.estop_active ? rgb565(160, 0, 0)
                       : (!g_homed)     ? rgb565(100, 40, 0)
                       :                   rgb565(0, 30, 60);
    display_fill_rect(0, 0, SCR_W, status_h, status_bg);

    char buf[32];
    if (sp.estop_active) {
        snprintf(buf, sizeof(buf), "E-STOP");
    } else if (sp.state == SPINDLE_STATE_RUNNING) {
        snprintf(buf, sizeof(buf), "%3d RPM %s",
                 sp.rpm_actual, (sp.dir == SPINDLE_DIR_FWD) ? "FWD" : "REV");
    } else {
        snprintf(buf, sizeof(buf), "RPM: --");
    }
    int tw = display_text_width(buf, FONT_SM);
    display_string((SCR_W - tw) / 2, 2, buf, FONT_SM,
                   sp.estop_active ? COL_WHITE : COL_LIGHT_GREY, 0xFFFF);

    // ── Główny obszar DRO ──
    int content_top = status_h + 4;
    int content_h   = SCR_H - status_h - 8;
    int row_h       = content_h / 2;

    // Oś Z – górna połowa
    int z_y = content_top;
    {
        float zmm = stepper_get_position_mm();
        int zi = (int)zmm;
        int zd = (int)(fabsf(zmm - (float)zi) * 100.0f);
        bool zh = limits_axis_homed(AXIS_Z);
        uint16_t zc = zh ? DRO_Z_COLOR : COL_WARN;

        // Etykieta "Z"
        display_string(UI_PAD_X, z_y + 4, "Z", FONT_LG, zc, DRO_BG);

        // Wartość – maksymalnie duża
        snprintf(buf, sizeof(buf), "%+6d.%02d", zi, zd);
        int vw = display_text_width(buf, FONT_LG);
        display_string(SCR_W - vw - UI_PAD_X, z_y + 4, buf, FONT_LG, zc, DRO_BG);

        // Jednostka + status homingu
        snprintf(buf, sizeof(buf), "mm %s", zh ? "[OK]" : "[--]");
        display_string(SCR_W - display_text_width(buf, FONT_SM) - UI_PAD_X,
                       z_y + FONT_LG_H + 4, buf, FONT_SM,
                       zh ? COL_OK : COL_WARN, DRO_BG);
    }

    // Separator
    int sep_y = z_y + row_h;
    display_hline(UI_PAD_X, sep_y, SCR_W - UI_PAD_X * 2, rgb565(40, 40, 40));

    // Oś X – dolna połowa
    int x_y = sep_y + 6;
    if (g_axis_x) {
        float xmm = axis_get_position_mm(g_axis_x);
        int xi = (int)xmm;
        int xd = (int)(fabsf(xmm - (float)xi) * 100.0f);
        bool xh = limits_axis_homed(AXIS_X);
        uint16_t xc = xh ? DRO_X_COLOR : COL_WARN;

        display_string(UI_PAD_X, x_y + 4, "X", FONT_LG, xc, DRO_BG);
        snprintf(buf, sizeof(buf), "%+6d.%02d", xi, xd);
        int vw = display_text_width(buf, FONT_LG);
        display_string(SCR_W - vw - UI_PAD_X, x_y + 4, buf, FONT_LG, xc, DRO_BG);

        snprintf(buf, sizeof(buf), "mm %s", xh ? "[OK]" : "[--]");
        display_string(SCR_W - display_text_width(buf, FONT_SM) - UI_PAD_X,
                       x_y + FONT_LG_H + 4, buf, FONT_SM,
                       xh ? COL_OK : COL_WARN, DRO_BG);
    } else {
        display_string(UI_PAD_X, x_y + 4, "X: --", FONT_MD, COL_LABEL, DRO_BG);
    }

    // ── Stopka – podpowiedź ──
    int ft_y = SCR_H - FONT_SM_H - 4;
    if (dro_ui.dirty) {
        snprintf(buf, sizeof(buf), "Jasnosc: %d%% *", dro_ui.brightness);
    } else {
        snprintf(buf, sizeof(buf), "ENC=jasn  SW=menu  BTN1=E-STOP");
    }
    display_fill_rect(0, ft_y - 2, SCR_W, FONT_SM_H + 6, rgb565(15, 15, 15));
    display_string(UI_PAD_X, ft_y, buf, FONT_SM, COL_LABEL, 0xFFFF);

    display_flush();
}

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
        // Powrót na dashboard
        ui_menu_goto(SCREEN_MAIN);
        break;
    case ENCODER_EVT_BTN3_PRESS:
        // Zeruj Z
        stepper_reset_position();
        ui_menu_notify("Z = 0", COL_OK, 800);
        break;
    case ENCODER_EVT_BTN3_LONG:
        // Zeruj X
        if (g_axis_x) {
            axis_reset_position(g_axis_x);
            ui_menu_notify("X = 0", COL_OK, 800);
        }
        break;
    default:
        break;
    }
}
