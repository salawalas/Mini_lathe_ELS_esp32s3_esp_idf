// screen_homing.inc – Ekran bazowania osi
// Dołączany do ui_menu.c przez #include
//
// Zachowanie:
//   - Ostrzeżenie pełnoekranowe migające 1 Hz
//   - Pasek postępu (bateria) podczas procedury
//   - Po sukcesie: g_homed = true, krzyżyk znika ze wszystkich ekranów
//   - BTN2 = anuluj i wróć do menu
#include "homing_state.h"
// Czas trwania paska postępu (ms) – symulowany / rzeczywisty homing
#define HOMING_PROGRESS_MS  10000

static struct {
    bool     running;        // czy procedura trwa
    int32_t  start_ms;       // czas startu procedury
    bool     confirmed;      // czy użytkownik potwierdził SW długi
} homing_ui = {
    .running   = false,
    .start_ms  = 0,
    .confirmed = false,
};

// ── Rysunek paska postępu w stylu "ładowanie baterii" ────────
static void _draw_progress_bar(int32_t elapsed_ms)
{
    const int BAR_X = 20;
    const int BAR_Y = CONTENT_Y + CONTENT_H - 28;
    const int BAR_W = SCR_W - 40;
    const int BAR_H = 18;
    const int CAP_W = 6;

    // Tło i obramowanie paska
    display_fill_rect(BAR_X, BAR_Y, BAR_W, BAR_H, rgb565(30, 0, 0));
    display_draw_rect(BAR_X, BAR_Y, BAR_W, BAR_H, rgb565(180, 40, 40));

    // Czapeczka baterii
    display_fill_rect(BAR_X + BAR_W, BAR_Y + BAR_H / 4,
                      CAP_W, BAR_H / 2, rgb565(180, 40, 40));

    // Oblicz procent — jawne ograniczenie zakresu widoczne dla kompilatora
    int pct = (elapsed_ms > 0)
              ? (int)(elapsed_ms * 100L / HOMING_PROGRESS_MS)
              : 0;
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;

    // Wypełnienie
    int fill_w = (BAR_W - 4) * pct / 100;
    if (fill_w > 0)
        display_fill_rect(BAR_X + 2, BAR_Y + 2,
                          fill_w, BAR_H - 4, rgb565(220, 40, 40));

    // Procent tekstowo — jeden buf, int, %d
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", pct);
    display_string(BAR_X + BAR_W / 2 - 12, BAR_Y + 1,
                   buf, FONT_SM, COL_WHITE, 0xFFFF);
}

static void draw_homing(void)
{
    int32_t now = (int32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

    // Miganie 1 Hz – co 500 ms zmień tło
    bool blink_on = ((now / 500) % 2) == 0;
    uint16_t bg = blink_on ? rgb565(180, 0, 0) : rgb565(80, 0, 0);

    // Pełnoekranowe tło (bez standardowego nagłówka)
    display_fill_rect(0, 0, SCR_W, SCR_H, bg);

    if (!homing_ui.confirmed) {
        // ── Ekran ostrzeżenia przed startem ──────────────────
        const int MID_Y = SCR_H / 2;

        int uw = display_text_width("! UWAGA !", FONT_LG);
        display_string((SCR_W - uw) / 2, MID_Y - 40,
                       "! UWAGA !",
                       FONT_LG, COL_WHITE, 0xFFFF);

        display_string(8, MID_Y,
                       "Zachowaj ostroznosc!",
                       FONT_MD, COL_WHITE, 0xFFFF);

        display_string(8, MID_Y + 28,
                       "Bazowanie osi",
                       FONT_MD, COL_WHITE, 0xFFFF);

        display_string(8, SCR_H - FOOTER_H - 20,
                       "SW (dlugi) = START   BTN2 = Anuluj",
                       FONT_SM, rgb565(255, 200, 200), 0xFFFF);

    } else {
        // ── Ekran procedury bazowania ─────────────────────────
        int32_t elapsed = now - homing_ui.start_ms;

        display_string(8, 8,
                       "Bazowanie osi...",
                       FONT_MD, COL_WHITE, 0xFFFF);

        // 4g: Postęp per-oś — używa limits_axis_homed() gdy krańcówki aktywne
        char st_z[24], st_x[24];
        const char *icon_h = "[OK]";
        snprintf(st_z, sizeof(st_z), "Os Z: %s",
            limits_axis_homed(AXIS_Z) ? icon_h : "jedzie ->");
        snprintf(st_x, sizeof(st_x), "Os X: %s",
            limits_axis_homed(AXIS_X) ? icon_h : "jedzie ->");
        // Oś Z
        display_string(8, CONTENT_Y + 10,
                       st_z,
                       FONT_SM, rgb565(255, 220, 180), 0xFFFF);
        // Oś X
        display_string(8, CONTENT_Y + 30,
                       st_x,
                       FONT_SM, rgb565(255, 220, 180), 0xFFFF);

        // Pasek postępu
        _draw_progress_bar(elapsed);

        // Zakończenie
        if (elapsed >= HOMING_PROGRESS_MS) {
            // Procedura zakończona sukcesem
            g_homed = true;
            homing_ui.running   = false;
            homing_ui.confirmed = false;
            ui_menu_notify("Bazowanie OK!", COL_OK, 2000);
            ui_menu_goto(SCREEN_MAIN);
            return;
        }
    }

    // Stopka bez krzyżyka (ekran bazowania jest wyjątkiem)
    display_fill_rect(0, FOOTER_Y, SCR_W, FOOTER_H, rgb565(60, 0, 0));
    display_string(4, FOOTER_Y + UI_FOOTER_TEXT_Y,
                   "Bazowanie osi", FONT_SM, rgb565(255, 180, 180), 0xFFFF);

    display_flush();
}

static void handle_homing(encoder_event_t evt)
{
    // E-STOP zawsze działa
    if (evt == ENCODER_EVT_BTN1_LONG) {
        if (homing_ui.running) {
            axis_stop(g_axis_z);
            axis_stop(g_axis_x);
        }
        homing_ui.running   = false;
        homing_ui.confirmed = false;
        spindle_emergency_stop();
        ui_menu_notify("!!! E-STOP !!!", COL_RED, 3000);
        ui_menu_goto(SCREEN_MAIN);
        return;
    }

    if (!homing_ui.confirmed) {
        // Ekran ostrzeżenia – czeka na potwierdzenie
        switch (evt) {
            case ENCODER_EVT_SW_LONG:
                // Potwierdzenie – START procedury
                homing_ui.confirmed = true;
                homing_ui.running   = true;
                homing_ui.start_ms  = (int32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

                // ──────────────────────────────────────────────────────────
                //  RZECZYWISTY HOMING — odkomentuj po podłączeniu krańcówek
                //  i odkomentowaniu limits_init() w main.c (linia ~212).
                //
                //  limits_home_axis(g_axis_z, LIMIT_Z_MIN, AXIS_DIR_NEG, 100.0f);
                //  limits_home_axis(g_axis_x, LIMIT_X_MIN, AXIS_DIR_NEG, 40.0f);
                //
                //  Patrz: main.c (komentarz sekcji "Krańcówki / Bazowanie")
                // ──────────────────────────────────────────────────────────

                ui_menu_notify("Bazowanie w toku...", COL_WARN, 500);
                break;

            case ENCODER_EVT_BTN2_PRESS:
            case ENCODER_EVT_BTN2_LONG:
                homing_ui.confirmed = false;
                homing_ui.running   = false;
                ui_menu_goto(SCREEN_MENU);
                break;

            default:
                break;
        }

    } else {
        // Procedura trwa – tylko STOP jest możliwy
        if (evt == ENCODER_EVT_BTN1_PRESS || evt == ENCODER_EVT_BTN2_PRESS) {
            axis_stop(g_axis_z);
            axis_stop(g_axis_x);
            homing_ui.running   = false;
            homing_ui.confirmed = false;
            ui_menu_notify("Bazowanie przerwane!", COL_WARN, 2000);
            ui_menu_goto(SCREEN_MENU);
        }
    }
}