// ============================================================
//  ui_menu.c  –  System menu tokarki
//  Mini Lathe Controller
//
//  WAŻNE: używa display_compat.h (stare API RGB565 → nowe RGB888)
//  Każda funkcja draw_xxx() kończy się display_flush()
//  → aktualizacja całego ekranu jednym DMA burst → zero mrugania
// ============================================================

#include "ui_menu.h"
#include "display_compat.h" // ← display + wrapper RGB565→RGB888
#include "encoder.h"
#include "stepper.h"
#include "spindle.h"
#include "motion.h"
#include "axis.h"
#include "limits.h"

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "homing_state.h"

static const char *TAG = "UI";

// Globalny stan bazowania osi (extern w ui_menu.h)
volatile bool g_homed = false;

// ------------------------------------------------------------
//  Wymiary ekranu z menuconfig -> TFT Configuration
//  Adaptacyjne: działają od ST7735 160×128 do ST7796 480×320
// ------------------------------------------------------------
#define SCR_W DISPLAY_WIDTH
#define SCR_H DISPLAY_HEIGHT

#define HEADER_H (SCR_H / 10)
#define FOOTER_H (SCR_H / 10)
#define ROW_H (SCR_H / 8)
#define UI_PAD_X (SCR_W / 40)
#define UI_VALUE_X (SCR_W * 2 / 5)
#define COL_RIGHT_1 (SCR_W - SCR_W / 10)
#define COL_RIGHT_2 (SCR_W - SCR_W / 20)
#define UI_ROW_GAP (SCR_H / 128)

#define CONTENT_Y (HEADER_H + 2)
#define CONTENT_H (SCR_H - HEADER_H - FOOTER_H - 4)
#define FOOTER_Y (SCR_H - FOOTER_H)
#define UI_TEXT_Y ((ROW_H - FONT_SM_H) / 2)
#define UI_HEADER_TEXT_Y ((HEADER_H - FONT_SM_H) / 2)
#define UI_FOOTER_TEXT_Y ((FOOTER_H - FONT_SM_H) / 2)

// Kolory motywu (RGB565)
#define COL_HDR_MAIN RGB565(0, 80, 160)
#define COL_HDR_JOG RGB565(0, 110, 60)
#define COL_HDR_FEED RGB565(120, 70, 0)
#define COL_HDR_SPIN RGB565(140, 0, 0)
#define COL_HDR_SET RGB565(60, 60, 60)
#define COL_HDR_MENU RGB565(30, 30, 90)
#define COL_FOOTER RGB565(20, 20, 20)
#define COL_SEL RGB565(0, 80, 160)
#define COL_VAL COLOR_CYAN
#define COL_LABEL COLOR_LIGHT_GREY
#define COL_WARN COLOR_ORANGE
#define COL_OK COLOR_GREEN

// ------------------------------------------------------------
//  Stan globalny UI
// ------------------------------------------------------------
static struct
{
    screen_id_t current, previous;
    bool needs_redraw;
    char notify_msg[28];
    uint16_t notify_color;
    int32_t notify_until_ms, uptime_ms;
    uint8_t jog_speed_pct, jog_step_idx;
    float feed_mm_min;
    stepper_dir_t feed_dir;
    uint16_t spindle_rpm;
    spindle_dir_t spindle_dir;
    uint8_t settings_sel, menu_sel;
    uint8_t menu_top;
} ui = {
    .current = SCREEN_MAIN,
    .previous = SCREEN_MAIN,
    .needs_redraw = true,
    .notify_msg = "",
    .notify_color = COL_OK,
    .notify_until_ms = 0,
    .jog_speed_pct = 30,
    .jog_step_idx = 1,
    .feed_mm_min = 100.0f,
    .feed_dir = STEPPER_DIR_CW,
    .spindle_rpm = 50,
    .spindle_dir = SPINDLE_DIR_FWD,
    .settings_sel = 0,
    .menu_sel = 0,
    .menu_top = 0,
};

static const uint16_t JOG_STEPS[] = {1, 8, 16, 80, 160};
static const char *JOG_STEPS_LBL[] = {"1", "8", "16", "80", "160"};
#define JOG_STEPS_COUNT 5

static const char *MENU_NAMES[] = {
    "Dashboard",
    "JOG",
    "Posuw AUTO",
    "Wrzeciono",
    "Ustawienia",
    "Gwintowanie",
    "Os X",
    "Bazowanie osi",
    "Informacje",
};

// Order of screens presented in the menu (maps to screen_id_t)
static const screen_id_t MENU_ORDER[] = {
    SCREEN_MAIN, SCREEN_JOG, SCREEN_FEED, SCREEN_SPINDLE,
    SCREEN_SETTINGS, SCREEN_ELS, SCREEN_AXIS_X, SCREEN_HOMING, SCREEN_MENU};

// ------------------------------------------------------------
//  Helpers
// ------------------------------------------------------------
static void draw_header(const char *title, uint16_t bg)
{
    display_fill_rect(0, 0, SCR_W, HEADER_H, bg);
    display_draw_string(UI_PAD_X, UI_HEADER_TEXT_Y, title, COLOR_WHITE, bg, 1);
}

static void draw_footer(void)
{
    display_fill_rect(0, FOOTER_Y, SCR_W, FOOTER_H, COL_FOOTER);
    display_draw_hline(0, FOOTER_Y - 1, SCR_W, COLOR_LIGHT_GREY);
    // Krzyżyk ostrzegawczy – brak bazowania osi
    if (!g_homed) {
        display_string(SCR_W - 18, FOOTER_Y + UI_FOOTER_TEXT_Y,
                       "X", FONT_SM, rgb565(255, 40, 40), 0xFFFF);
    }
    const char *hints[] = {
        "SW=menu",
        "SW=OK BTN2=back",
        "ENC=krok SW=go",
        "BTN3=start BTN1=stop",
        "ENC=RPM BTN3=start",
        "ENC=val SW=save",
        "SW>>>=START",
        "ENC=jog SW=tryb",
    };
    if (ui.uptime_ms < ui.notify_until_ms && ui.notify_msg[0])
    {
        display_draw_string(UI_PAD_X, FOOTER_Y + UI_FOOTER_TEXT_Y, ui.notify_msg,
                            ui.notify_color, COL_FOOTER, 1);
    }
    else
    {
        display_draw_string(UI_PAD_X, FOOTER_Y + UI_FOOTER_TEXT_Y, hints[ui.current],
                            COLOR_LIGHT_GREY, COL_FOOTER, 1);
    }
}

static void draw_row(int16_t y, const char *label, const char *val,
                     uint16_t vc, bool sel)
{
    uint16_t bg = sel ? COL_SEL : COLOR_BLACK;
    display_fill_rect(0, y + UI_ROW_GAP, SCR_W, ROW_H - (2 * UI_ROW_GAP), bg);
    display_draw_string(UI_PAD_X, y + UI_TEXT_Y, label, COL_LABEL, bg, 1);
    display_draw_string(UI_VALUE_X, y + UI_TEXT_Y, val, vc, bg, 1);
}

// ------------------------------------------------------------
//  EKRAN: MAIN
// ------------------------------------------------------------
static void draw_main(void)
{
    spindle_status_t sp;
    spindle_get_status(&sp);
    float pos_mm = stepper_get_position_mm();

    uint16_t hdr = sp.estop_active ? COLOR_RED : (sp.state == SPINDLE_STATE_RUNNING) ? COL_HDR_SPIN
                                                                                     : COL_HDR_MAIN;
    draw_header(sp.estop_active ? "!!! E-STOP !!!" : "MINI LATHE", hdr);
    display_fill_rect(0, HEADER_H, SCR_W, CONTENT_H + 2, COLOR_BLACK);

    char buf[24];
    int16_t y = CONTENT_Y;

    // RPM
    display_draw_string(4, y, "RPM:", COL_LABEL, COLOR_BLACK, 1);
    snprintf(buf, sizeof(buf), "%3d/%3d", sp.rpm_actual, sp.rpm_target);
    display_draw_string(UI_PAD_X + 32, y, buf, sp.at_speed ? COL_OK : COL_WARN, COLOR_BLACK, 1);
    snprintf(buf, sizeof(buf), "PWR:%s", sp.power_enabled ? "ON" : "OFF");
    display_draw_string(COL_RIGHT_1, y, buf, sp.power_enabled ? COL_OK : COL_LABEL, COLOR_BLACK, 1);
    y += ROW_H;

    // Pozycja Z + status homingu
    display_draw_string(4, y, "Z:", COL_LABEL, COLOR_BLACK, 1);
    int pi = (int)pos_mm, pd = (int)(fabsf(pos_mm - (float)pi) * 100.0f);
    snprintf(buf, sizeof(buf), "%4d.%02d mm", pi, pd);
    display_draw_string(UI_PAD_X + 14, y, buf, limits_axis_homed(AXIS_Z) ? COL_VAL : COL_WARN, COLOR_BLACK, 1);
    const char *fs[] = {"IDLE", "RUN ", "ACC ", "DEC ", "ERR "};
    display_draw_string(COL_RIGHT_1, y, fs[stepper_get_state()],
                        (stepper_get_state() == STEPPER_STATE_IDLE) ? COL_LABEL : COL_WARN, COLOR_BLACK, 1);
    // Znacznik homingu
    snprintf(buf, sizeof(buf), "%s", limits_axis_homed(AXIS_Z) ? "[OK]" : "[--]");
    display_draw_string(COL_RIGHT_2, y, buf, limits_axis_homed(AXIS_Z) ? COL_OK : COL_WARN, COLOR_BLACK, 1);
    y += ROW_H;

    // Pozycja X + status homingu
    if (g_axis_x)
    {
        display_draw_string(4, y, "X:", COL_LABEL, COLOR_BLACK, 1);
        float xmm = axis_get_position_mm(g_axis_x);
        int xi = (int)xmm, xd = (int)(fabsf(xmm - (float)xi) * 100.0f);
        snprintf(buf, sizeof(buf), "%4d.%02d mm", xi, xd);
        display_draw_string(UI_PAD_X + 14, y, buf, limits_axis_homed(AXIS_X) ? COL_CYAN : COL_WARN, COLOR_BLACK, 1);
        snprintf(buf, sizeof(buf), "%s", limits_axis_homed(AXIS_X) ? "[OK]" : "[--]");
        display_draw_string(COL_RIGHT_2, y, buf, limits_axis_homed(AXIS_X) ? COL_OK : COL_WARN, COLOR_BLACK, 1);
        y += ROW_H;
    }

    display_draw_hline(4, y, SCR_W - 8, COLOR_LIGHT_GREY);
    y += 4;

    // Krańcówki – ostrzeżenie jeśli wyzwolone
    if (limits_any_triggered())
    {
        display_fill_rect(0, y, SCR_W, 14, COLOR_RED);
        display_draw_string(4, y + 2, "!!! KRANCOWKA !!!", COLOR_WHITE, COLOR_RED, 1);
        y += 16;
    }

    // Prędkość posuwu
    uint32_t spd = stepper_get_speed();
    if (spd > 0)
    {
        snprintf(buf, sizeof(buf), "V: %.1f mm/min", (float)spd / axis_get_steps_per_mm(g_axis_z) * 60.0f);
    }
    else
    {
        snprintf(buf, sizeof(buf), "V: STOP");
    }
    display_draw_string(4, y, buf, spd ? COLOR_YELLOW : COL_LABEL, COLOR_BLACK, 1);

    draw_footer();
    display_flush();
}

static void handle_main(encoder_event_t evt)
{
    switch (evt)
    {
    case ENCODER_EVT_SW_PRESS:
        ui_menu_goto(SCREEN_MENU);
        break;
    case ENCODER_EVT_BTN1_PRESS:
    case ENCODER_EVT_BTN1_LONG:
        spindle_emergency_stop();
        stepper_stop();
        ui_menu_notify("!!! E-STOP !!!", COLOR_RED, 3000);
        break;
    case ENCODER_EVT_BTN2_PRESS:
        if (spindle_estop_is_active())
        {
            spindle_estop_reset();
            ui_menu_notify(spindle_estop_is_active() ? "Zwolnij wylacznik!" : "E-STOP reset OK",
                           spindle_estop_is_active() ? COLOR_RED : COL_OK, 1500);
        }
        else
        {
            ui_menu_goto(SCREEN_JOG);
        }
        break;
    case ENCODER_EVT_BTN3_PRESS:
        ui_menu_goto(SCREEN_SPINDLE);
        break;
    default:
        break;
    }
}

// ------------------------------------------------------------
//  EKRAN: MENU
// ------------------------------------------------------------
static void draw_menu(void)
{
    draw_header("WYBIERZ TRYB", COL_HDR_MENU);
    display_fill_rect(0, HEADER_H, SCR_W, CONTENT_H + 2, COLOR_BLACK);
    int menu_count = sizeof(MENU_ORDER) / sizeof(MENU_ORDER[0]);
    int rows = (FOOTER_Y - CONTENT_Y) / ROW_H; // rows per column
    int cols = 2;
    int col_w = SCR_W / cols;
    // Render two columns so all menu items fit on one screen
    for (int i = 0; i < menu_count; i++)
    {
        int row = i % rows;
        int col = i / rows; // 0 = left, 1 = right
        int16_t y = CONTENT_Y + row * ROW_H;
        int16_t x = col * col_w + 2;
        bool sel = (i == ui.menu_sel);
        uint16_t bg = sel ? COL_SEL : COLOR_BLACK;
        display_fill_rect(x, y + UI_ROW_GAP, col_w - 4, ROW_H - (2 * UI_ROW_GAP), bg);
        if (sel)
            display_draw_string(x, y + UI_TEXT_Y, ">", COLOR_WHITE, bg, 1);
        display_draw_string(x + 12, y + UI_TEXT_Y, MENU_NAMES[i],
                            sel ? COLOR_WHITE : COL_LABEL, bg, 1);
    }
    draw_footer();
    display_flush();
}

static void handle_menu(encoder_event_t evt)
{
    int menu_count = sizeof(MENU_ORDER) / sizeof(MENU_ORDER[0]);
    switch (evt)
    {
    case ENCODER_EVT_CW:
        ui.menu_sel = (ui.menu_sel + 1) % menu_count;
        break;
    case ENCODER_EVT_CCW:
        ui.menu_sel = (ui.menu_sel == 0) ? (menu_count - 1) : (ui.menu_sel - 1);
        break;
    case ENCODER_EVT_SW_PRESS:
    case ENCODER_EVT_BTN3_PRESS:
        ui_menu_goto(MENU_ORDER[ui.menu_sel]);
        break;
    case ENCODER_EVT_SW_LONG:
    case ENCODER_EVT_BTN2_PRESS:
        ui_menu_goto(SCREEN_MAIN);
        break;
    default:
        break;
    }
}

// ------------------------------------------------------------
//  EKRAN: JOG
// ------------------------------------------------------------
static void draw_jog(void)
{
    draw_header("RECZNY JOG", COL_HDR_JOG);
    display_fill_rect(0, HEADER_H, SCR_W, CONTENT_H + 2, COLOR_BLACK);
    char buf[20];
    int16_t y = CONTENT_Y;
    float pos = stepper_get_position_mm();
    int pi = (int)pos, pd = (int)(fabsf(pos - (float)pi) * 100.0f);
    display_draw_string(UI_PAD_X, y, "Z:", COL_LABEL, COLOR_BLACK, 1);
    snprintf(buf, sizeof(buf), "%5d.%02d mm", pi, pd);
    display_draw_string(UI_PAD_X + 14, y, buf, COL_VAL, COLOR_BLACK, 2);
    y += 20;
    display_draw_hline(4, y, SCR_W - 8, COLOR_LIGHT_GREY);
    y += 5;
    snprintf(buf, sizeof(buf), "Krok: %s kr", JOG_STEPS_LBL[ui.jog_step_idx]);
    draw_row(y, buf, "", COL_LABEL, false);
    y += ROW_H;
    snprintf(buf, sizeof(buf), "%d%%", ui.jog_speed_pct);
    draw_row(y, "Predkosc:", buf, COLOR_YELLOW, false);
    y += ROW_H;
    display_draw_hline(4, y, SCR_W - 8, COLOR_LIGHT_GREY);
    y += 5;
    display_draw_string(4, y, "ENC=jog SW=krok BTN2/3=pred",
                        COLOR_LIGHT_GREY, COLOR_BLACK, 1);
    draw_footer();
    display_flush();
}

static void handle_jog(encoder_event_t evt)
{
    switch (evt)
    {
    case ENCODER_EVT_CW:
        stepper_jog(STEPPER_DIR_CW, JOG_STEPS[ui.jog_step_idx], ui.jog_speed_pct);
        break;
    case ENCODER_EVT_CCW:
        stepper_jog(STEPPER_DIR_CCW, JOG_STEPS[ui.jog_step_idx], ui.jog_speed_pct);
        break;
    case ENCODER_EVT_SW_PRESS:
        ui.jog_step_idx = (ui.jog_step_idx + 1) % JOG_STEPS_COUNT;
        ui_menu_notify("Krok zmieniony", COL_OK, 600);
        break;
    case ENCODER_EVT_SW_LONG:
        stepper_reset_position();
        ui_menu_notify("Pozycja Z = 0", COL_OK, 800);
        break;
    case ENCODER_EVT_BTN1_PRESS:
        stepper_stop();
        spindle_emergency_stop();
        ui_menu_notify("!!! E-STOP !!!", COLOR_RED, 2000);
        break;
    case ENCODER_EVT_BTN2_PRESS:
        if (ui.jog_speed_pct > 10)
            ui.jog_speed_pct -= 10;
        break;
    case ENCODER_EVT_BTN3_PRESS:
        if (ui.jog_speed_pct < 100)
            ui.jog_speed_pct += 10;
        break;
    case ENCODER_EVT_BTN2_LONG:
        ui_menu_goto(SCREEN_MENU);
        break;
    default:
        break;
    }
}

// ------------------------------------------------------------
//  EKRAN: FEED
// ------------------------------------------------------------
static void draw_feed(void)
{
    draw_header("POSUW AUTO", COL_HDR_FEED);
    display_fill_rect(0, HEADER_H, SCR_W, CONTENT_H + 2, COLOR_BLACK);
    char buf[24];
    int16_t y = CONTENT_Y;
    stepper_state_t st = stepper_get_state();
    float pos = stepper_get_position_mm();
    const char *sn[] = {"STOP", "BIEG", "ROZP", "HAM", "BLAD"};
    uint16_t sc[] = {COL_LABEL, COL_OK, COL_WARN, COL_WARN, COLOR_RED};
    snprintf(buf, sizeof(buf), "Stan: %s", sn[st]);
    display_draw_string(4, y, buf, sc[st], COLOR_BLACK, 1);
    y += ROW_H;
    int pi = (int)pos, pd = (int)(fabsf(pos - (float)pi) * 100.0f);
    snprintf(buf, sizeof(buf), "Z: %4d.%02d mm", pi, pd);
    display_draw_string(4, y, buf, COL_VAL, COLOR_BLACK, 1);
    y += ROW_H;
    display_draw_hline(4, y, SCR_W - 8, COLOR_LIGHT_GREY);
    y += 5;
    snprintf(buf, sizeof(buf), "%5.1f", ui.feed_mm_min);
    draw_row(y, "mm/min:", buf, COLOR_YELLOW, true);
    y += ROW_H;
    draw_row(y, "Kierunek:", (ui.feed_dir == STEPPER_DIR_CW) ? "CW >" : "CCW <", COL_WARN, false);
    y += ROW_H;
    display_draw_hline(4, y, SCR_W - 8, COLOR_LIGHT_GREY);
    y += 5;
    display_draw_string(4, y, (st == STEPPER_STATE_IDLE) ? "BTN3=START BTN2=DIR" : "BTN1=STOP",
                        (st == STEPPER_STATE_IDLE) ? COL_OK : COL_WARN, COLOR_BLACK, 1);
    draw_footer();
    display_flush();
}

static void handle_feed(encoder_event_t evt)
{
    switch (evt)
    {
    case ENCODER_EVT_CW:
        ui.feed_mm_min += 10.0f;
        if (ui.feed_mm_min > 500.0f)
            ui.feed_mm_min = 500.0f;
        break;
    case ENCODER_EVT_CCW:
        ui.feed_mm_min -= 10.0f;
        if (ui.feed_mm_min < 10.0f)
            ui.feed_mm_min = 10.0f;
        break;
    case ENCODER_EVT_BTN3_PRESS:
        if (stepper_get_state() == STEPPER_STATE_IDLE)
        {
            stepper_run(ui.feed_dir, ui.feed_mm_min);
            ui_menu_notify("Posuw START", COL_OK, 600);
        }
        break;
    case ENCODER_EVT_BTN1_PRESS:
        stepper_stop();
        ui_menu_notify("Posuw STOP", COL_WARN, 600);
        break;
    case ENCODER_EVT_BTN1_LONG:
        stepper_stop();
        spindle_emergency_stop();
        ui_menu_notify("!!! E-STOP !!!", COLOR_RED, 2000);
        break;
    case ENCODER_EVT_BTN2_PRESS:
        ui.feed_dir = (ui.feed_dir == STEPPER_DIR_CW) ? STEPPER_DIR_CCW : STEPPER_DIR_CW;
        break;
    case ENCODER_EVT_BTN2_LONG:
        stepper_stop();
        ui_menu_goto(SCREEN_MENU);
        break;
    case ENCODER_EVT_SW_LONG:
        stepper_reset_position();
        ui_menu_notify("Pozycja = 0", COL_OK, 800);
        break;
    default:
        break;
    }
}

// ------------------------------------------------------------
//  EKRAN: SPINDLE
// ------------------------------------------------------------
static void draw_spindle(void)
{
    spindle_status_t sp;
    spindle_get_status(&sp);
    draw_header("WRZECIONO", COL_HDR_SPIN);
    display_fill_rect(0, HEADER_H, SCR_W, CONTENT_H + 2, COLOR_BLACK);
    char buf[24];
    int16_t y = CONTENT_Y;
    snprintf(buf, sizeof(buf), "%3d RPM", sp.rpm_actual);
    display_draw_string(UI_PAD_X, y, "Actual:", COL_LABEL, COLOR_BLACK, 1);
    display_draw_string(UI_PAD_X + 48, y, buf, sp.at_speed ? COL_OK : COL_WARN, COLOR_BLACK, 2);
    y += 20;
    display_draw_hline(4, y, SCR_W - 8, COLOR_LIGHT_GREY);
    y += 5;
    snprintf(buf, sizeof(buf), "%4d", ui.spindle_rpm);
    draw_row(y, "Zadane:", buf, COLOR_YELLOW, true);
    y += ROW_H;
    snprintf(buf, sizeof(buf), "%5lu kr/s", (unsigned long)sp.steps_per_s);
    draw_row(y, "STEP:", buf, COL_VAL, false);
    y += ROW_H;
    const char *ds[] = {"STOP", "BIEG", "ROZP", "HAM", "E-STP", "BLAD"};
    const char *dirs = (ui.spindle_dir == SPINDLE_DIR_FWD) ? "FWD" : "REV";
    snprintf(buf, sizeof(buf), "%s %s", dirs, (sp.state < 6) ? ds[sp.state] : "???");
    draw_row(y, "Stan:", buf,
             (sp.state == SPINDLE_STATE_RUNNING) ? COL_OK : (sp.state == SPINDLE_STATE_ESTOP) ? COLOR_RED
                                                                                              : COL_LABEL,
             false);
    y += ROW_H;
    snprintf(buf, sizeof(buf), sp.power_enabled ? "ON (36V)" : "OFF");
    draw_row(y, "Zasilanie:", buf, sp.estop_active ? COLOR_RED : sp.power_enabled ? COL_OK
                                                                                  : COL_LABEL,
             false);
    y += ROW_H;
    display_draw_hline(4, y, SCR_W - 8, COLOR_LIGHT_GREY);
    y += 5;
    if (sp.estop_active)
        display_draw_string(4, y, "BTN2=reset E-STOP", COLOR_RED, COLOR_BLACK, 1);
    else if (sp.state == SPINDLE_STATE_STOPPED)
        display_draw_string(4, y, "BTN3=START  BTN2=DIR", COL_OK, COLOR_BLACK, 1);
    else
        display_draw_string(4, y, "BTN1=STOP  ENC=RPM", COL_WARN, COLOR_BLACK, 1);
    draw_footer();
    display_flush();
}

static void handle_spindle(encoder_event_t evt)
{
    switch (evt)
    {
    case ENCODER_EVT_CW:
        ui.spindle_rpm += 10;
        if (ui.spindle_rpm > SPINDLE_RPM_MAX)
            ui.spindle_rpm = SPINDLE_RPM_MAX;
        if (spindle_get_rpm() > 0)
        {
            spindle_set_rpm(ui.spindle_rpm);
        }
        break;
    case ENCODER_EVT_CCW:
        if (ui.spindle_rpm > SPINDLE_RPM_MIN + 10)
            ui.spindle_rpm -= 10;
        else
            ui.spindle_rpm = SPINDLE_RPM_MIN;
        if (spindle_get_rpm() > 0)
        {
            spindle_set_rpm(ui.spindle_rpm);
        }
        break;
    case ENCODER_EVT_BTN3_PRESS:
        if (!spindle_estop_is_active())
        {
            spindle_start(ui.spindle_rpm, ui.spindle_dir);
            ui_menu_notify("Wrzeciono START", COL_OK, 600);
        }
        else
        {
            ui_menu_notify("E-STOP aktywny!", COLOR_RED, 1500);
        }
        break;
    case ENCODER_EVT_BTN1_PRESS:
        spindle_stop();
        ui_menu_notify("Wrzeciono STOP", COL_WARN, 600);
        break;
    case ENCODER_EVT_BTN1_LONG:
        spindle_emergency_stop();
        stepper_stop();
        ui_menu_notify("!!! E-STOP !!!", COLOR_RED, 3000);
        break;
    case ENCODER_EVT_BTN2_PRESS:
        if (spindle_estop_is_active())
        {
            spindle_estop_reset();
            ui_menu_notify(spindle_estop_is_active() ? "Zwolnij wylacznik!" : "E-STOP reset OK",
                           spindle_estop_is_active() ? COLOR_RED : COL_OK, 1500);
        }
        else
        {
            ui.spindle_dir = (ui.spindle_dir == SPINDLE_DIR_FWD) ? SPINDLE_DIR_REV : SPINDLE_DIR_FWD;
        }
        break;
    case ENCODER_EVT_BTN2_LONG:
        spindle_stop();
        ui_menu_goto(SCREEN_MENU);
        break;
    case ENCODER_EVT_SW_LONG:
        ui_menu_goto(SCREEN_MENU);
        break;
    default:
        break;
    }
}

// ------------------------------------------------------------
//  EKRAN: SETTINGS
// ------------------------------------------------------------
static struct
{
    const char *label;
    float value, step, min_val, max_val;
    const char *unit;
} s_set[] = {
    {"Skok Z", 2.0f, 0.25f, 0.5f, 8.0f, "mm"},
    {"Skok X", 1.25f, 0.25f, 0.5f, 8.0f, "mm"},
    {"Mikrokrok", 12800.0f, 800.0f, 800.0f, 25600.0f, "kr"},
    {"Maks V", 300.0f, 10.0f, 10.0f, 500.0f, "mm/m"},
    {"RPM max", 3000.0f, 100.0f, 100.0f, 5000.0f, "RPM"},
};
#define SET_COUNT 5

// ── NVS persistence ──
static const char *NVS_KEYS[] = {"lead_z", "lead_x", "ustep", "max_v", "rpm_max"};

static void settings_nvs_load(void)
{
    nvs_handle_t h;
    if (nvs_open("lathe", NVS_READONLY, &h) != ESP_OK)
        return;
    for (int i = 0; i < SET_COUNT; i++)
    {
        float v = 0.0f;
        size_t blob_len = sizeof(v);
        if (nvs_get_blob(h, NVS_KEYS[i], &v, &blob_len) == ESP_OK && blob_len == sizeof(v) && v >= s_set[i].min_val && v <= s_set[i].max_val)
            s_set[i].value = v;
    }
    nvs_close(h);
    ESP_LOGI(TAG, "Ustawienia wczytane z NVS");
}

static void settings_nvs_save(void)
{
    nvs_handle_t h;
    if (nvs_open("lathe", NVS_READWRITE, &h) != ESP_OK)
        return;
    for (int i = 0; i < SET_COUNT; i++)
    {
        float v = s_set[i].value;
        nvs_set_blob(h, NVS_KEYS[i], &v, sizeof(v));
    }
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "Ustawienia zapisane w NVS");
}

static void draw_settings(void)
{
    draw_header("USTAWIENIA", COL_HDR_SET);
    display_fill_rect(0, HEADER_H, SCR_W, CONTENT_H + 2, COLOR_BLACK);
    char buf[16];
    int16_t y = CONTENT_Y;
    for (int i = 0; i < SET_COUNT; i++)
    {
        snprintf(buf, sizeof(buf), "%.2f %s", s_set[i].value, s_set[i].unit);
        draw_row(y, s_set[i].label, buf, (i == ui.settings_sel) ? COLOR_WHITE : COL_VAL, i == ui.settings_sel);
        y += ROW_H;
    }
    display_draw_hline(4, y + 2, SCR_W - 8, COLOR_LIGHT_GREY);
    display_draw_string(4, y + 5, "ENC=val SW=next BTN3=save", COLOR_LIGHT_GREY, COLOR_BLACK, 1);
    draw_footer();
    display_flush();
}

static void handle_settings(encoder_event_t evt)
{
    switch (evt)
    {
    case ENCODER_EVT_CW:
        s_set[ui.settings_sel].value += s_set[ui.settings_sel].step;
        if (s_set[ui.settings_sel].value > s_set[ui.settings_sel].max_val)
        {
            s_set[ui.settings_sel].value = s_set[ui.settings_sel].max_val;
        }
        break;
    case ENCODER_EVT_CCW:
        s_set[ui.settings_sel].value -= s_set[ui.settings_sel].step;
        if (s_set[ui.settings_sel].value < s_set[ui.settings_sel].min_val)
        {
            s_set[ui.settings_sel].value = s_set[ui.settings_sel].min_val;
        }
        break;
    case ENCODER_EVT_SW_PRESS:
        ui.settings_sel = (ui.settings_sel + 1) % SET_COUNT;
        break;
    case ENCODER_EVT_BTN3_PRESS:
        axis_set_lead_mm(g_axis_z, s_set[0].value);
        axis_set_lead_mm(g_axis_x, s_set[1].value);
        stepper_set_microstep((uint16_t)s_set[2].value);
        axis_set_max_speed_steps_s(g_axis_z,
                                   (uint32_t)(s_set[3].value / 60.0f * axis_get_steps_per_mm(g_axis_z)));
        spindle_set_max_rpm((uint16_t)s_set[4].value);
        settings_nvs_save();
        ui_menu_notify("Zapisano!", COL_OK, 1500);
        break;
    case ENCODER_EVT_BTN2_LONG:
    case ENCODER_EVT_SW_LONG:
        ui_menu_goto(SCREEN_MENU);
        break;
    case ENCODER_EVT_BTN2_PRESS:
        if (ui.settings_sel > 0)
            ui.settings_sel--;
        break;
    default:
        break;
    }
}

// ------------------------------------------------------------
//  EKRAN: ELS
// ------------------------------------------------------------
#include "screen_els.inc"
#include "screen_homing.inc"

// ------------------------------------------------------------
//  EKRAN: OŚ X
// ------------------------------------------------------------
#include "screen_axis_x.inc"

// ------------------------------------------------------------
//  Tablica ekranów
// ------------------------------------------------------------
typedef void (*draw_fn_t)(void);
typedef void (*handle_fn_t)(encoder_event_t);

static const draw_fn_t s_draw[] = {
    draw_main, draw_menu, draw_jog, draw_feed, draw_spindle,
    draw_settings, draw_els, draw_axis_x, draw_homing};
static const handle_fn_t s_handle[] = {
    handle_main, handle_menu, handle_jog, handle_feed, handle_spindle,
    handle_settings, handle_els, handle_axis_x, handle_homing};

// ------------------------------------------------------------
//  E-STOP callback z ISR spindle
// ------------------------------------------------------------
static void IRAM_ATTR ui_estop_cb(void *arg)
{
    ui.needs_redraw = true;
}

// ------------------------------------------------------------
//  Główny task UI
// ------------------------------------------------------------
static void ui_task(void *arg)
{
    display_fill(COLOR_BLACK);
    s_draw[ui.current]();
    TickType_t last_redraw = xTaskGetTickCount();

    while (1)
    {
        encoder_msg_t msg;
        bool got = encoder_get_event(&msg, pdMS_TO_TICKS(50));
        ui.uptime_ms = (int32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

        // E-STOP ostrzeżenie cykliczne
        if (spindle_estop_is_active() && ui.uptime_ms > ui.notify_until_ms)
            ui_menu_notify("!!! E-STOP BTN2=reset", COLOR_RED, 2000);

        if (got)
        {
            s_handle[ui.current](msg.type);
            ui.needs_redraw = true;
        }

        TickType_t now = xTaskGetTickCount();
        if ((now - last_redraw) >= pdMS_TO_TICKS(200))
        {
            ui.needs_redraw = true;
            last_redraw = now;
        }

        if (ui.needs_redraw)
        {
            s_draw[ui.current]();
            ui.needs_redraw = false;
        }
    }
}

// ------------------------------------------------------------
//  API publiczne
// ------------------------------------------------------------
void ui_menu_init(void)
{
    ESP_LOGI(TAG, "Inicjalizacja UI Menu (%d ekranow)...", SCREEN_COUNT);
    ui.current = SCREEN_MAIN;
    ui.needs_redraw = true;

    // Wczytaj ustawienia z NVS i zastosuj do sprzętu
    settings_nvs_load();
    axis_set_lead_mm(g_axis_z, s_set[0].value);
    axis_set_lead_mm(g_axis_x, s_set[1].value);
    axis_set_max_speed_steps_s(g_axis_z,
                               (uint32_t)(s_set[3].value / 60.0f * axis_get_steps_per_mm(g_axis_z)));
    spindle_set_max_rpm((uint16_t)s_set[4].value);

    spindle_register_estop_callback(ui_estop_cb, NULL);
    xTaskCreate(ui_task, "ui_task", 6144, NULL, 4, NULL);
}

void ui_menu_goto(screen_id_t screen)
{
    if (screen >= SCREEN_COUNT)
        return;
    ui.previous = ui.current;
    ui.current = screen;
    ui.needs_redraw = true;
    display_fill(COLOR_BLACK);
}

screen_id_t ui_menu_current_screen(void) { return ui.current; }

void ui_menu_notify(const char *msg, uint16_t color, uint32_t ms)
{
    strncpy(ui.notify_msg, msg, sizeof(ui.notify_msg) - 1);
    ui.notify_msg[sizeof(ui.notify_msg) - 1] = '\0';
    ui.notify_color = color;
    ui.notify_until_ms = ui.uptime_ms + (int32_t)ms;
    ui.needs_redraw = true;
}
