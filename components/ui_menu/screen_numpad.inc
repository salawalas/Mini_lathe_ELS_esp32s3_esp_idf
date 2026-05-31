// screen_numpad.inc – Klawiatura numeryczna (optymalna dla ≥320×240)
// Dołączany do ui_menu.c przez #include
//
// Wywołanie: numpad_open("RPM", 60.0f, 10.0f, 3000.0f, 1.0f, "RPM");
// Odczyt:    if (numpad_done) { float val = numpad_value; }

#include <stdio.h>
#include <string.h>

#define NUMPAD_MAX_DIGITS 12

static struct {
    char     title[20];
    char     buf[NUMPAD_MAX_DIGITS + 2];  // digits + '.' + '\0'
    uint8_t  len;
    float    min_val, max_val, step;
    char     unit[8];
    bool     active;
    bool     done;
    bool     dot_used;
    float    result;
} np = {0};

// ── Public API ─────────────────────────────────────────────

void numpad_open(const char *title, float initial, float min_v, float max_v,
                 float step, const char *unit)
{
    np.active  = true;
    np.done    = false;
    np.dot_used = false;
    np.len     = 0;
    np.min_val = min_v;
    np.max_val = max_v;
    np.step    = step;
    snprintf(np.title, sizeof(np.title), "%s", title);
    snprintf(np.unit,  sizeof(np.unit),  "%s", unit);
    // Konwertuj initial na string
    if (initial == 0.0f) {
        np.buf[0] = '0'; np.buf[1] = '\0'; np.len = 1;
    } else {
        snprintf(np.buf, sizeof(np.buf), "%.1f", initial);
        np.len = strlen(np.buf);
        np.dot_used = (strchr(np.buf, '.') != NULL);
    }
}

bool numpad_is_done(void)   { return np.done; }
float numpad_get_value(void) { return np.result; }

// ── Drawing ────────────────────────────────────────────────

// Układ: 4 kolumny × 4 wiersze (0-9, ., ←, OK, Anuluj)
// Skalowanie: dopasuj do SCR_W i SCR_H
static int np_btn_w(void) { return (SCR_W - 6 * 4) / 4; }  // 4 kolumny
static int np_btn_h(void) { return (SCR_H - HEADER_H - 30) / 5; } // 5 wierszy
static int np_x(int col)  { return 4 + col * (np_btn_w() + 4); }
static int np_y(int row)  { return HEADER_H + 4 + 20 + row * (np_btn_h() + 3); }

static void np_draw_btn(int col, int row, const char *label, uint16_t color, uint16_t bg)
{
    int x = np_x(col), y = np_y(row), w = np_btn_w(), h = np_btn_h();
    display_fill_rect(x, y, w, h, bg);
    display_draw_rect(x, y, w, h, color);
    int tw = display_text_width(label, FONT_LG);
    display_draw_string(x + (w - tw) / 2, y + (h - FONT_LG_H) / 2,
                       label, color, bg, 1);
}

static void draw_numpad(void)
{
    if (!np.active) return;

    char hdr[32];
    snprintf(hdr, sizeof(hdr), "%s %s", np.title, np.unit[0] ? np.unit : "");
    draw_header(hdr, COL_HDR_SET);
    display_fill_rect(0, HEADER_H, SCR_W, SCR_H - HEADER_H, COLOR_BLACK);

    // Pole edycji
    int ey = np_y(0) - 26;
    display_fill_rect(4, ey, SCR_W - 8, 22, rgb565(10, 14, 20));
    display_draw_rect(4, ey, SCR_W - 8, 22, COL_VAL);

    char display_buf[20];
    snprintf(display_buf, sizeof(display_buf), "%s", np.buf[0] ? np.buf : "0");
    int tw = display_text_width(display_buf, FONT_MD);
    display_draw_string(SCR_W - tw - 12, ey + 3, display_buf, COL_VAL, rgb565(10, 14, 20), 1);

    uint16_t btn_bg = rgb565(25, 30, 42);
    uint16_t btn_ok_bg  = rgb565(0, 40, 20);
    uint16_t btn_clr_bg = rgb565(50, 20, 20);

    // Wiersz 0: 7 8 9 ←
    np_draw_btn(0, 0, "7", COL_LABEL, btn_bg);
    np_draw_btn(1, 0, "8", COL_LABEL, btn_bg);
    np_draw_btn(2, 0, "9", COL_LABEL, btn_bg);
    np_draw_btn(3, 0, "<-", COLOR_RED, btn_clr_bg);

    // Wiersz 1: 4 5 6 .
    np_draw_btn(0, 1, "4", COL_LABEL, btn_bg);
    np_draw_btn(1, 1, "5", COL_LABEL, btn_bg);
    np_draw_btn(2, 1, "6", COL_LABEL, btn_bg);
    np_draw_btn(3, 1, ".", COL_VAL, btn_bg);

    // Wiersz 2: 1 2 3 0
    np_draw_btn(0, 2, "1", COL_LABEL, btn_bg);
    np_draw_btn(1, 2, "2", COL_LABEL, btn_bg);
    np_draw_btn(2, 2, "3", COL_LABEL, btn_bg);
    np_draw_btn(3, 2, "0", COL_LABEL, btn_bg);

    // Wiersz 3: Anuluj OK
    // "Anuluj" zajmuje 2 kolumny, "OK" 2 kolumny
    {
        int x0 = np_x(0), y3 = np_y(3), w = np_btn_w(), h = np_btn_h();
        int w2 = w * 2 + 4;
        display_fill_rect(x0, y3, w2, h, btn_clr_bg);
        display_draw_rect(x0, y3, w2, h, COLOR_RED);
        const char *clr = "Anuluj";
        tw = display_text_width(clr, FONT_MD);
        display_draw_string(x0 + (w2 - tw) / 2, y3 + (h - FONT_MD_H) / 2,
                           clr, COLOR_RED, btn_clr_bg, 1);

        int xok = np_x(2), wok = w * 2 + 4;
        display_fill_rect(xok, y3, wok, h, btn_ok_bg);
        display_draw_rect(xok, y3, wok, h, COL_OK);
        const char *ok = "OK";
        tw = display_text_width(ok, FONT_LG);
        display_draw_string(xok + (wok - tw) / 2, y3 + (h - FONT_LG_H) / 2,
                           ok, COL_OK, btn_ok_bg, 1);
    }

    draw_footer();
    display_flush();
}

// ── Input handler (via encoder / touch) ────────────────────

static void numpad_add_char(char c)
{
    if (np.len >= NUMPAD_MAX_DIGITS) return;
    if (c == '.' && np.dot_used) return;
    if (c == '.') np.dot_used = true;
    np.buf[np.len++] = c;
    np.buf[np.len] = '\0';
}

static void numpad_backspace(void)
{
    if (np.len == 0) return;
    if (np.buf[np.len - 1] == '.') np.dot_used = false;
    np.len--;
    np.buf[np.len] = '\0';
}

static void numpad_accept(void)
{
    float val = (float)atof(np.buf);
    if (val < np.min_val) val = np.min_val;
    if (val > np.max_val) val = np.max_val;
    np.result = val;
    np.done   = true;
    np.active = false;
}

static void numpad_cancel(void)
{
    np.result = 0.0f;
    np.done   = false;
    np.active = false;
}

// ── Handle (called from ui_task) ───────────────────────────

static void handle_numpad(encoder_event_t evt)
{
    if (!np.active) return;

    // Map encoder events to virtual "press" of digit, backspace, OK, cancel
    switch (evt) {
    case ENCODER_EVT_SW_PRESS:
        numpad_accept();
        break;
    case ENCODER_EVT_SW_LONG:
        numpad_cancel();
        break;
    case ENCODER_EVT_BTN2_PRESS:
        numpad_backspace();
        break;
    case ENCODER_EVT_CW:
        // increment value by step
        {
            float v = (float)atof(np.buf) + np.step;
            if (v > np.max_val) v = np.max_val;
            if (np.step >= 1.0f)
                snprintf(np.buf, sizeof(np.buf), "%.0f", v);
            else
                snprintf(np.buf, sizeof(np.buf), "%.1f", v);
            np.len = strlen(np.buf);
            np.dot_used = (strchr(np.buf, '.') != NULL);
        }
        break;
    case ENCODER_EVT_CCW:
        {
            float v = (float)atof(np.buf) - np.step;
            if (v < np.min_val) v = np.min_val;
            if (np.step >= 1.0f)
                snprintf(np.buf, sizeof(np.buf), "%.0f", v);
            else
                snprintf(np.buf, sizeof(np.buf), "%.1f", v);
            np.len = strlen(np.buf);
            np.dot_used = (strchr(np.buf, '.') != NULL);
        }
        break;
    default:
        break;
    }
}

// ── Helper: wykryj dotknięcie przycisku i wykonaj akcję ───
void numpad_touch_event(uint16_t tx, uint16_t ty)
{
    if (!np.active) return;

    // Sprawdź, który przycisk został dotknięty
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 4; col++) {
            int bx = np_x(col), by = np_y(row), bw = np_btn_w(), bh = np_btn_h();
            // Ostatni wiersz ma dwie duże kolumny
            if (row == 3) {
                int w2 = np_btn_w() * 2 + 4;
                if (tx >= bx && tx < bx + w2 && ty >= by && ty < by + bh) {
                    numpad_cancel();  // lewe 2 kolumny = Anuluj
                    return;
                }
                // Prawe 2 kolumny = OK (col 2,3)
                bx = np_x(2);
                w2 = np_btn_w() * 2 + 4;
                if (tx >= bx && tx < bx + w2 && ty >= by && ty < by + bh) {
                    numpad_accept();
                    return;
                }
                continue;
            }
            if (tx >= bx && tx < bx + bw && ty >= by && ty < by + bh) {
                // Który klawisz?
                const char *keys[4][4] = {
                    {"7","8","9","<-"},
                    {"4","5","6","."},
                    {"1","2","3","0"},
                };
                const char *k = keys[row][col];
                if (k[0] == '<') numpad_backspace();
                else numpad_add_char(k[0]);
                return;
            }
        }
    }
}
