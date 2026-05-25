// screen_position.inc – Ekran Pozycja / Presety osi
// Dołączany do ui_menu.c przez #include
//
// Tryby param_sel:
//   0 – Z (ustaw wartość, BTN3=zastosuj)
//   1 – X (ustaw wartość, BTN3=zastosuj)
//   2 – preset P1 (SW=skocz, BTN3=zapisz tu)
//   3 – preset P2
//   4 – preset P3

#define POS_PRESET_COUNT 3

static const char *POS_NVS_KEYS[] = {"p1_z","p1_x","p2_z","p2_x","p3_z","p3_x"};

static struct {
    uint8_t  param_sel;
    float    edit_z, edit_x;       // edytowane wartości Z i X
    float    preset_z[POS_PRESET_COUNT];
    float    preset_x[POS_PRESET_COUNT];
    bool     presets_loaded;
} pos_ui = {0};

static void pos_presets_nvs_load(void)
{
    nvs_handle_t h;
    if (nvs_open("lathe", NVS_READONLY, &h) != ESP_OK) return;
    for (int i = 0; i < POS_PRESET_COUNT; i++) {
        float vz = 0, vx = 0;
        size_t sz = sizeof(float);
        nvs_get_blob(h, POS_NVS_KEYS[i*2],   &vz, &sz);
        nvs_get_blob(h, POS_NVS_KEYS[i*2+1], &vx, &sz);
        pos_ui.preset_z[i] = vz;
        pos_ui.preset_x[i] = vx;
    }
    nvs_close(h);
    pos_ui.presets_loaded = true;
}

static void pos_presets_nvs_save(int idx)
{
    nvs_handle_t h;
    if (nvs_open("lathe", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, POS_NVS_KEYS[idx*2],   &pos_ui.preset_z[idx], sizeof(float));
    nvs_set_blob(h, POS_NVS_KEYS[idx*2+1], &pos_ui.preset_x[idx], sizeof(float));
    nvs_commit(h);
    nvs_close(h);
}

static void draw_position(void)
{
    if (!pos_ui.presets_loaded) {
        pos_ui.edit_z = axis_get_position_mm(g_axis_z);
        pos_ui.edit_x = g_axis_x ? axis_get_position_mm(g_axis_x) : 0;
        pos_presets_nvs_load();
    }

    draw_header("POZYCJA / PRESETY", COL_HDR_POSITION);
    display_fill_rect(0, HEADER_H, SCR_W, CONTENT_H+2, COLOR_BLACK);

    char buf[24];
    int16_t y = CONTENT_Y + 2;

    // Pozycja Z – edytowalna
    int zi = (int)pos_ui.edit_z, zd = (int)(fabsf(pos_ui.edit_z - (float)zi) * 100.0f);
    snprintf(buf, sizeof(buf), "%+4d.%02d mm", zi, zd);
    draw_row(y, "Z:", buf, COLOR_YELLOW, pos_ui.param_sel == 0);
    y += ROW_H;

    // Pozycja X – edytowalna
    int xi = (int)pos_ui.edit_x, xd = (int)(fabsf(pos_ui.edit_x - (float)xi) * 100.0f);
    snprintf(buf, sizeof(buf), "%+4d.%02d mm", xi, xd);
    draw_row(y, "X:", buf, COLOR_CYAN, pos_ui.param_sel == 1);
    y += ROW_H;

    display_draw_hline(4, y, SCR_W - 8, COLOR_LIGHT_GREY);
    y += 5;

    // Presety
    for (int i = 0; i < POS_PRESET_COUNT; i++) {
        bool sel = ((int)pos_ui.param_sel == i + 2);
        snprintf(buf, sizeof(buf), "P%d: Z%+.1f X%+.1f", i+1,
                 pos_ui.preset_z[i], pos_ui.preset_x[i]);
        if (sel) {
            display_fill_rect(0, y, SCR_W, ROW_H, rgb565(0, 40, 80));
            display_draw_string(UI_PAD_X, y + UI_TEXT_Y, buf, COLOR_WHITE, rgb565(0,40,80), 1);
        } else {
            display_draw_string(UI_PAD_X, y + UI_TEXT_Y, buf, COL_LABEL, COLOR_BLACK, 1);
        }
        y += ROW_H;
    }

    display_draw_hline(4, y, SCR_W - 8, COLOR_LIGHT_GREY);
    y += 5;

    // Podpowiedź
    if (pos_ui.param_sel <= 1)
        display_draw_string(4, y, "ENC=val BTN3=ustaw SW=dalej", COL_LABEL, COLOR_BLACK, 1);
    else
        display_draw_string(4, y, "SW=skocz BTN3=zapisz tu", COL_LABEL, COLOR_BLACK, 1);

    draw_footer();
    display_flush();
}

static void handle_position(encoder_event_t evt)
{
    if (evt == ENCODER_EVT_BTN1_LONG) {
        spindle_emergency_stop();
        stepper_stop();
        ui_menu_notify("!!! E-STOP !!!", COLOR_RED, 3000);
        ui_menu_goto(SCREEN_MAIN);
        return;
    }

    char buf[24];
    switch (evt) {
    case ENCODER_EVT_CW:
        if (pos_ui.param_sel == 0) pos_ui.edit_z += 0.1f;
        else if (pos_ui.param_sel == 1) pos_ui.edit_x += 0.05f;
        break;
    case ENCODER_EVT_CCW:
        if (pos_ui.param_sel == 0) pos_ui.edit_z -= 0.1f;
        else if (pos_ui.param_sel == 1) pos_ui.edit_x -= 0.05f;
        break;
    case ENCODER_EVT_SW_PRESS:
        pos_ui.param_sel = (pos_ui.param_sel + 1) % (2 + POS_PRESET_COUNT);
        break;
    case ENCODER_EVT_SW_LONG:
        // Skocz do wybranego presetu (tylko jeśli param_sel >= 2)
        if (pos_ui.param_sel >= 2) {
            int idx = pos_ui.param_sel - 2;
            if (axis_get_state(g_axis_z) == AXIS_STATE_IDLE) {
                axis_set_position(g_axis_z, pos_ui.preset_z[idx]);
                snprintf(buf, sizeof(buf), "P%d: Z ustawione", idx + 1);
                ui_menu_notify(buf, COL_OK, 1000);
            }
            if (g_axis_x && axis_get_state(g_axis_x) == AXIS_STATE_IDLE) {
                axis_set_position(g_axis_x, pos_ui.preset_x[idx]);
            }
        }
        break;
    case ENCODER_EVT_BTN1_PRESS:
        // Zapisz bieżącą pozycję do edytowanych wartości
        pos_ui.edit_z = axis_get_position_mm(g_axis_z);
        pos_ui.edit_x = g_axis_x ? axis_get_position_mm(g_axis_x) : 0;
        ui_menu_notify("Pozycja odczytana", COL_OK, 600);
        break;
    case ENCODER_EVT_BTN3_PRESS:
        if (pos_ui.param_sel <= 1) {
            // Zastosuj wartość Z lub X
            if (pos_ui.param_sel == 0) {
                axis_set_position(g_axis_z, pos_ui.edit_z);
                snprintf(buf, sizeof(buf), "Z = %.2f mm", pos_ui.edit_z);
                ui_menu_notify(buf, COL_OK, 1000);
            } else if (pos_ui.param_sel == 1 && g_axis_x) {
                axis_set_position(g_axis_x, pos_ui.edit_x);
                snprintf(buf, sizeof(buf), "X = %.2f mm", pos_ui.edit_x);
                ui_menu_notify(buf, COL_OK, 1000);
            }
        } else {
            // Zapisz bieżącą pozycję do presetu
            int idx = pos_ui.param_sel - 2;
            pos_ui.preset_z[idx] = axis_get_position_mm(g_axis_z);
            pos_ui.preset_x[idx] = g_axis_x ? axis_get_position_mm(g_axis_x) : 0;
            pos_presets_nvs_save(idx);
            snprintf(buf, sizeof(buf), "P%d zapisany", idx + 1);
            ui_menu_notify(buf, COL_OK, 1000);
        }
        break;
    case ENCODER_EVT_BTN2_LONG:
        ui_menu_goto(SCREEN_MENU);
        break;
    default:
        break;
    }
}