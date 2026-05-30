// wifi_server.c – WiFi AP + HTTP/JSON API + embedded DRO page
#include "wifi_server.h"
#include "axis.h"
#include "stepper.h"
#include "spindle.h"
#include "homing_state.h"

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include <sys/param.h>

static const char *TAG = "WIFI";

// ── WiFi credentials – ZMIEŃ PRZED UŻYCIEM ─────────────────
// Domyślne wartości. Można też przechowywać w NVS (klucz "wifi_ssid" / "wifi_pass").
#define AP_SSID_DEFAULT     "Mini_Lathe"
#define AP_PASS_DEFAULT     "123465780"    // minimum 8 znaków dla WPA2
#define AP_CHANNEL          6
#define AP_MAX_CONN         4

// ── Embedded HTML page ─────────────────────────────────────
static const char INDEX_HTML[] =
"<!DOCTYPE html><html><head>"
"<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1.0'>"
"<title>Mini Lathe DRO</title>"
"<style>"
"*{box-sizing:border-box;margin:0;padding:0}"
"body{background:#111;color:#ddd;font:16px monospace;padding:10px;max-width:480px;margin:0 auto}"
"h1{font-size:18px;color:#888;margin-bottom:8px}"
".dro{font-size:42px;font-weight:bold;margin:8px 0;line-height:1.2}"
".z{color:#ff0}.x{color:#0ff}"
".rpm{font-size:24px;color:#0f0;margin:4px 0}"
".warn{color:#f60}.estop{color:#f00;background:#400;padding:6px;border-radius:4px;font-size:20px;text-align:center}"
"button{font-size:16px;padding:8px 14px;margin:3px;border:none;border-radius:4px;background:#444;color:#fff;min-width:52px}"
"button:active{opacity:.7}"
".btn-stop{background:#c00;font-weight:bold}"
".btn-go{background:#080}"
".btn-estop{background:#c00;font-size:24px;padding:14px 28px;width:100%;margin:10px 0}"
".row{display:flex;align-items:center;gap:6px;margin:6px 0;flex-wrap:wrap}"
"input[type=range]{width:130px;accent-color:#0a0}"
"#ip{color:#666;font-size:12px;text-align:center;margin-top:12px}"
"#status_bar{text-align:center;padding:4px;border-radius:4px;margin-bottom:6px;font-weight:bold}"
"</style></head><body>"
"<h1>⚙ Mini Lathe v6.1</h1>"
"<div id='status_bar'>Łączenie...</div>"
"<div class='dro'><div class='z'>Z: <span id='z'>--</span> mm</div>"
"<div class='x'>X: <span id='x'>--</span> mm</div></div>"
"<div class='rpm'>⭯ <span id='rpm'>--</span> RPM <span id='spstat'></span></div>"
"<div class='row'>"
"<button onclick='j(\"Z\",\"+\",1)'>Z+1</button>"
"<button onclick='j(\"Z\",\"-\",1)'>Z-1</button>"
"<button onclick='j(\"Z\",\"+\",80)'>Z+80</button>"
"<button onclick='j(\"Z\",\"-\",80)'>Z-80</button></div>"
"<div class='row'>"
"<button onclick='j(\"X\",\"+\",1)'>X+1</button>"
"<button onclick='j(\"X\",\"-\",1)'>X-1</button>"
"<button onclick='j(\"X\",\"+\",16)'>X+16</button>"
"<button onclick='j(\"X\",\"-\",16)'>X-16</button></div>"
"<div class='row'>"
"<input type='range' id='rs' min='10' max='120' value='50' oninput='document.getElementById(\"rv\").textContent=this.value'>"
"<span id='rv'>50</span> RPM"
"<button class='btn-go' onclick='sp(\"start\")'>▶</button>"
"<button class='btn-stop' onclick='sp(\"stop\")'>■</button>"
"<button class='btn-go' onclick='sp(\"rev\")'>◀REV</button></div>"
"<button class='btn-estop' onclick='es()'>🛑 E-STOP</button>"
"<div id='ip'>WiFi: MiniLathe / 192.168.4.1</div>"
"<script>"
"async function poll(){try{let r=await fetch('/api/status');let s=await r.json();"
"d('z',s.z_mm.toFixed(2));d('x',s.x_mm.toFixed(2));d('rpm',s.rpm);"
"let b=document.getElementById('status_bar');"
"if(s.estop){b.textContent='🛑 E-STOP AKTYWNY!';b.className='estop'}"
"else if(!s.homed){b.textContent='⚠ Osie niezbazowane';b.className='warn'}"
"else if(s.spindle_on){b.textContent='⚙ Praca';b.className=''}"
"else{b.textContent='Gotowy';b.className=''}"
"d('spstat',s.spindle_on?'ON':'OFF')}catch(e){}}"
"function d(id,v){document.getElementById(id).textContent=v}"
"async function j(axis,dir,steps){await fetch('/api/jog?axis='+axis+'&dir='+dir+'&steps='+steps)}"
"async function sp(cmd){let rpm=document.getElementById('rs').value;await fetch('/api/spindle?cmd='+cmd+'&rpm='+rpm)}"
"async function es(){await fetch('/api/estop')}"
"setInterval(poll,200);poll()"
"</script></body></html>";

// ── REST: /api/status ──────────────────────────────────────
static esp_err_t api_status_handler(httpd_req_t *req)
{
    spindle_status_t sp;
    spindle_get_status(&sp);

    float z_mm = stepper_get_position_mm();
    float x_mm = g_axis_x ? axis_get_position_mm(g_axis_x) : 0.0f;

    char buf[512];
    snprintf(buf, sizeof(buf),
        "{\"z_mm\":%.3f,\"x_mm\":%.3f,\"rpm\":%d,\"rpm_target\":%d,"
        "\"spindle_on\":%s,\"estop\":%s,\"homed\":%s,\"power\":%s}",
        z_mm, x_mm,
        sp.rpm_actual, sp.rpm_target,
        (sp.state >= SPINDLE_STATE_RAMPING_UP && sp.state <= SPINDLE_STATE_RUNNING) ? "true" : "false",
        sp.estop_active ? "true" : "false",
        g_homed ? "true" : "false",
        sp.power_enabled ? "true" : "false");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// ── REST: /api/jog?axis=Z&dir=+&steps=10 ──────────────────
static esp_err_t api_jog_handler(httpd_req_t *req)
{
    char axis[4] = "Z", dir[2] = "+";
    int  steps = 1;
    bool ok = false;

    char value[32];
    if (httpd_req_get_url_query_str(req, value, sizeof(value)) == ESP_OK)
    {
        char param[16];
        if (httpd_query_key_value(value, "axis", param, sizeof(param)) == ESP_OK)
            strncpy(axis, param, sizeof(axis) - 1);
        if (httpd_query_key_value(value, "dir", param, sizeof(param)) == ESP_OK)
            strncpy(dir, param, sizeof(dir) - 1);
        if (httpd_query_key_value(value, "steps", param, sizeof(param)) == ESP_OK)
            steps = atoi(param);
    }

    if (steps <= 0) steps = 1;
    if (steps > 500) steps = 500;

    axis_dir_t adir = (dir[0] == '+') ? AXIS_DIR_POS : AXIS_DIR_NEG;

    if (axis[0] == 'X' || axis[0] == 'x') {
        if (g_axis_x && axis_get_state(g_axis_x) == AXIS_STATE_IDLE) {
            axis_jog(g_axis_x, adir, (uint16_t)steps, 40);
            ok = true;
        }
    } else {
        if (stepper_get_state() == STEPPER_STATE_IDLE) {
            axis_jog(g_axis_z, adir, (uint16_t)steps, 40);
            ok = true;
        }
    }

    const char *resp = ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"axis busy\"}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// ── REST: /api/spindle?cmd=start&rpm=50 ────────────────────
static esp_err_t api_spindle_handler(httpd_req_t *req)
{
    char cmd[16] = "";
    int  rpm = 50;

    char value[64];
    if (httpd_req_get_url_query_str(req, value, sizeof(value)) == ESP_OK) {
        char param[16];
        if (httpd_query_key_value(value, "cmd", param, sizeof(param)) == ESP_OK)
            strncpy(cmd, param, sizeof(cmd) - 1);
        if (httpd_query_key_value(value, "rpm", param, sizeof(param)) == ESP_OK)
            rpm = atoi(param);
    }

    if (rpm < SPINDLE_RPM_MIN) rpm = SPINDLE_RPM_MIN;
    if (rpm > SPINDLE_RPM_MAX) rpm = SPINDLE_RPM_MAX;

    bool ok = true;
    if (strcmp(cmd, "start") == 0) {
        spindle_start((uint16_t)rpm, SPINDLE_DIR_FWD);
    } else if (strcmp(cmd, "rev") == 0) {
        spindle_start((uint16_t)rpm, SPINDLE_DIR_REV);
    } else if (strcmp(cmd, "stop") == 0) {
        spindle_stop();
    } else {
        ok = false;
    }

    char buf[64];
    snprintf(buf, sizeof(buf), "{\"ok\":%s,\"rpm\":%d}", ok ? "true" : "false", rpm);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// ── REST: /api/estop ───────────────────────────────────────
static esp_err_t api_estop_handler(httpd_req_t *req)
{
    spindle_emergency_stop();
    stepper_stop();
    if (g_axis_x) axis_stop(g_axis_x);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// ── GET / (index page) ─────────────────────────────────────
static esp_err_t index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// ── HTTP server setup ──────────────────────────────────────
static httpd_handle_t s_server = NULL;

static void start_http_server(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 8;
    cfg.stack_size = 8192;
    cfg.max_open_sockets = 4;
    cfg.lru_purge_enable = true;

    httpd_uri_t uris[] = {
        {.uri="/",              .method=HTTP_GET, .handler=index_handler},
        {.uri="/api/status",    .method=HTTP_GET, .handler=api_status_handler},
        {.uri="/api/jog",       .method=HTTP_GET, .handler=api_jog_handler},
        {.uri="/api/spindle",   .method=HTTP_GET, .handler=api_spindle_handler},
        {.uri="/api/estop",     .method=HTTP_GET, .handler=api_estop_handler},
    };

    if (httpd_start(&s_server, &cfg) == ESP_OK) {
        for (int i = 0; i < sizeof(uris)/sizeof(uris[0]); i++)
            httpd_register_uri_handler(s_server, &uris[i]);
        ESP_LOGI(TAG, "HTTP server started on port 80");
    } else {
        ESP_LOGE(TAG, "Failed to start HTTP server");
    }
}

// ── WiFi event handler ─────────────────────────────────────
static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
        ESP_LOGI(TAG, "Klient polaczony");
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STADISCONNECTED) {
        ESP_LOGI(TAG, "Klient rozlaczony");
    }
}

// ── Init ───────────────────────────────────────────────────
esp_err_t wifi_server_init(void)
{
    // Wycisz debug logi httpd i wifi (tylko INFO i wyższe)
    esp_log_level_set("httpd", ESP_LOG_INFO);
    esp_log_level_set("httpd_parse", ESP_LOG_INFO);
    esp_log_level_set("httpd_txrx", ESP_LOG_INFO);
    esp_log_level_set("httpd_sess", ESP_LOG_INFO);
    esp_log_level_set("httpd_uri", ESP_LOG_INFO);
    esp_log_level_set("wifi", ESP_LOG_INFO);
    esp_log_level_set("esp_netif_lwip", ESP_LOG_INFO);
    esp_log_level_set("event", ESP_LOG_WARN);

    ESP_LOGI(TAG, "Inicjalizacja Wi-Fi AP...");

    // Wczytaj credentials z NVS (fallback → #define)
    char ssid[33] = AP_SSID_DEFAULT;
    char pass[65] = AP_PASS_DEFAULT;
    {
        nvs_handle_t h;
        if (nvs_open("lathe", NVS_READONLY, &h) == ESP_OK) {
            size_t sz = sizeof(ssid);
            nvs_get_str(h, "wifi_ssid", ssid, &sz);
            sz = sizeof(pass);
            nvs_get_str(h, "wifi_pass", pass, &sz);
            nvs_close(h);
        }
    }
    // Wymuś minimum 8 znaków dla WPA2
    if (strlen(pass) < 8) {
        strcpy(pass, AP_PASS_DEFAULT);
        ESP_LOGW(TAG, "Haslo za krotkie – przywrocono domyslne");
    }

    // Init netif (only once)
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wcfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));

    wifi_config_t ap_cfg = {
        .ap = {
            .ssid = "",
            .password = "",
            .ssid_len = (uint8_t)strlen(ssid),
            .channel = AP_CHANNEL,
            .max_connection = AP_MAX_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
        },
    };
    strncpy((char *)ap_cfg.ap.ssid, ssid, sizeof(ap_cfg.ap.ssid) - 1);
    strncpy((char *)ap_cfg.ap.password, pass, sizeof(ap_cfg.ap.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "AP '%s' gotowy (WPA2). IP: 192.168.4.1", ssid);

    start_http_server();
    return ESP_OK;
}
