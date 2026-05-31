// ble_server.c – BLE GATT server via NimBLE: DRO telemetry + JSON commands
#include "ble_server.h"
#include "axis.h"
#include "stepper.h"
#include "spindle.h"
#include "homing_state.h"

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_sm.h"
#include "host/util/util.h" // ← ble_hs_util_ensure_addr
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "store/config/ble_store_config.h"

// Forward declaration — w niektórych wersjach IDF nie ma jej w headerze:
extern void ble_store_config_init(void);

static const char *TAG = "BLE";

#define DEVICE_NAME "MiniLathe"

#define GATT_SVC_UUID 0x00FF
#define GATT_CHR_DRO_UUID 0xFF01
#define GATT_CHR_CMD_UUID 0xFF02

static bool s_notify_enabled = false;
static uint16_t s_dro_handle = 0;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE; // ← DODANE

// ── Build DRO JSON ──────────────────────────────────────────
static void build_dro_json(char *buf, size_t max_len)
{
    spindle_status_t sp;
    spindle_get_status(&sp);
    float z_mm = stepper_get_position_mm();
    float x_mm = g_axis_x ? axis_get_position_mm(g_axis_x) : 0.0f;

    snprintf(buf, max_len,
             "{\"z\":%.2f,\"x\":%.2f,\"rpm\":%d,\"estop\":%d,\"homed\":%d,\"spindle\":%d}",
             z_mm, x_mm, sp.rpm_actual,
             sp.estop_active ? 1 : 0,
             g_homed ? 1 : 0,
             (sp.state >= SPINDLE_STATE_RAMPING_UP &&
              sp.state <= SPINDLE_STATE_RUNNING)
                 ? 1
                 : 0);
}

// ── Parse JSON command ──────────────────────────────────────
static void exec_ble_command(const char *json, int len)
{
    char cmd[16] = "", axis[4] = "Z", dir[4] = "fwd";
    int steps = 1, rpm = 0;

    const char *p = strstr(json, "\"cmd\"");
    if (p)
    {
        p = strchr(p, ':');
        if (p)
        {
            p++;
            while (*p == ' ' || *p == '"')
                p++;
            int i = 0;
            while (*p && *p != '"' && *p != ',' && *p != '}' && i < 15)
                cmd[i++] = *p++;
            cmd[i] = 0;
        }
    }

    p = strstr(json, "\"axis\"");
    if (p)
    {
        p = strchr(p, ':');
        if (p)
        {
            p++;
            while (*p == ' ' || *p == '"')
                p++;
            if (*p)
            {
                axis[0] = *p;
                axis[1] = 0;
            }
        }
    }

    // "steps" = mikrokroki, "mm" = milimetry (przeliczamy na kroki)
    p = strstr(json, "\"mm\"");
    if (p) {
        p = strchr(p, ':');
        if (p) {
            float mm = (float)atof(p + 1);
            // Użyj poprawnej osi do przeliczenia mm→kroki
            float spm = (axis[0] == 'X' || axis[0] == 'x')
                        ? axis_get_steps_per_mm(g_axis_x)
                        : axis_get_steps_per_mm(g_axis_z);
            steps = (int)(mm * spm);
        }
    }
    p = strstr(json, "\"steps\"");
    if (p && steps == 0)  // "steps" ma priorytet tylko gdy "mm" nie było
    {
        p = strchr(p, ':');
        if (p)
            steps = atoi(p + 1);
    }

    p = strstr(json, "\"rpm\"");
    if (p)
    {
        p = strchr(p, ':');
        if (p)
            rpm = atoi(p + 1);
    }

    p = strstr(json, "\"dir\"");
    if (p)
    {
        p = strchr(p, ':');
        if (p)
        {
            p++;
            while (*p == ' ' || *p == '"')
                p++;
            int i = 0;
            while (*p && *p != '"' && *p != ',' && i < 3)
                dir[i++] = *p++;
            dir[i] = 0;
        }
    }

    if (strcmp(cmd, "estop") == 0)
    {
        spindle_emergency_stop();
        stepper_stop();
        if (g_axis_x)
            axis_stop(g_axis_x);
    }
    else if (strcmp(cmd, "jog") == 0) {
        if (steps == 0)
            steps = 1;
        if (steps > 64000)
            steps = 64000;
        axis_dir_t adir = (steps > 0) ? AXIS_DIR_POS : AXIS_DIR_NEG;
        uint16_t abs_s = (uint16_t)(steps > 0 ? steps : -steps);
        if (axis[0] == 'X' || axis[0] == 'x')
        {
            if (g_axis_x && axis_get_state(g_axis_x) == AXIS_STATE_IDLE)
                axis_jog(g_axis_x, adir, abs_s, 80);
        }
        else
        {
            if (stepper_get_state() == STEPPER_STATE_IDLE)
                axis_jog(g_axis_z, adir, abs_s, 80);
        }
    }
    else if (strcmp(cmd, "spindle") == 0)
    {
        if (rpm > 0)
        {
            spindle_dir_t sdir = (dir[0] == 'r' || dir[0] == 'R')
                                     ? SPINDLE_DIR_REV
                                     : SPINDLE_DIR_FWD;
            spindle_start((uint16_t)rpm, sdir);
        }
        else
        {
            spindle_stop();
        }
    }
}

// ── GATT access callback ────────────────────────────────────
static int gatt_svr_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR && attr_handle == s_dro_handle)
    {
        char buf[256];
        build_dro_json(buf, sizeof(buf));
        int rc = os_mbuf_append(ctxt->om, buf, strlen(buf));
        return (rc == 0) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR)
    {
        if (ctxt->om)
        {
            char buf[128] = {0};
            int len = OS_MBUF_PKTLEN(ctxt->om);
            if (len > 127)
                len = 127;
            os_mbuf_copydata(ctxt->om, 0, len, buf);
            exec_ble_command(buf, len);
        }
        return 0;
    }

    return 0;
}

// ── GATT service definition ─────────────────────────────────
static const struct ble_gatt_chr_def gatt_chrs[] = {
    {
        .uuid = BLE_UUID16_DECLARE(GATT_CHR_DRO_UUID),
        .access_cb = gatt_svr_access_cb,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &s_dro_handle,
    },
    {
        .uuid = BLE_UUID16_DECLARE(GATT_CHR_CMD_UUID),
        .access_cb = gatt_svr_access_cb,
        .flags = BLE_GATT_CHR_F_WRITE,
    },
    {0}};

static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(GATT_SVC_UUID),
        .characteristics = gatt_chrs,
    },
    {0}};

// ── GAP event handler ───────────────────────────────────────
static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    switch (event->type)
    {

    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0)
        {
            s_conn_handle = event->connect.conn_handle; // ← DODANE
            ESP_LOGI(TAG, "Klient BLE polaczony (handle=%d)", s_conn_handle);
        }
        else
        {
            ESP_LOGW(TAG, "Polaczenie nieudane, status=%d", event->connect.status);
        }
        s_notify_enabled = false;
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Klient BLE rozlaczony (reason=%d)",
                 event->disconnect.reason);
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_notify_enabled = false;

        // Restart advertising ze świeżymi danymi
        ble_gap_adv_stop();
        vTaskDelay(pdMS_TO_TICKS(200));  // daj kontrolerowi chwilę

        struct ble_hs_adv_fields fields = {0};
        fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
        fields.name = (void *)DEVICE_NAME;
        fields.name_len = strlen(DEVICE_NAME);
        fields.name_is_complete = 1;
        ble_gap_adv_set_fields(&fields);

        struct ble_gap_adv_params adv_params = {0};
        adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
        adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
        ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                          &adv_params, gap_event_cb, NULL);
        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
        if (event->enc_change.status == 0)
        {
            ESP_LOGI(TAG, "Szyfrowanie OK");
        }
        else
        {
            // Niespójny/zepsuty bond → rozłącz, daj szansę na czyste połączenie
            ESP_LOGW(TAG, "Szyfrowanie nieudane (status=%d) — rozlaczam",
                     event->enc_change.status);
            ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        }
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == s_dro_handle)
        {
            s_notify_enabled = event->subscribe.cur_notify;
            ESP_LOGI(TAG, "DRO notify: %s", s_notify_enabled ? "ON" : "OFF");
        }
        return 0;

    default:
        return 0;
    }
}

// ── Notify task ─────────────────────────────────────────────
static void ble_notify_task(void *arg)
{
    char buf[256];
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(300));
        if (!s_notify_enabled || s_dro_handle == 0 || s_conn_handle == BLE_HS_CONN_HANDLE_NONE) // ← POPRAWIONE
            continue;

        build_dro_json(buf, sizeof(buf));
        struct os_mbuf *om = ble_hs_mbuf_from_flat(buf, strlen(buf));
        if (om)
        {
            ble_gattc_notify_custom(s_conn_handle, s_dro_handle, om); // ← POPRAWIONE
        }
    }
}

// ── Host sync callback ──────────────────────────────────────
static void on_sync(void)
{
    // Użyj adresu publicznego (spójnie z adv_start poniżej)
    int rc = ble_hs_util_ensure_addr(0);
    assert(rc == 0);

    ble_svc_gap_device_name_set(DEVICE_NAME);

    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (void *)DEVICE_NAME;
    fields.name_len = strlen(DEVICE_NAME);
    fields.name_is_complete = 1;
    ble_gap_adv_set_fields(&fields);

    struct ble_gap_adv_params adv_params = {0};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                      &adv_params, gap_event_cb, NULL);
    ESP_LOGI(TAG, "BLE advertising started jako '%s'", DEVICE_NAME);
}

// ── Host task ────────────────────────────────────────────────
static void host_task_fn(void *arg)
{
    nimble_port_run();
}

esp_err_t ble_server_init(void)
{
    ESP_LOGI(TAG, "Inicjalizacja BLE (NimBLE)...");

    // Wycisz NimBLE logi (notify co 300ms spamuje INFO)
    esp_log_level_set("NimBLE", ESP_LOG_WARN);

    // Wyczyść stare klucze bonding (nimble_bond w NVS)
    {
        nvs_handle_t h;
        if (nvs_open("nimble_bond", NVS_READWRITE, &h) == ESP_OK) {
            nvs_erase_all(h);
            nvs_commit(h);
            nvs_close(h);
            ESP_LOGI(TAG, "Stare klucze bonding wyczyszczone");
        }
    }

    nimble_port_init();
    ble_hs_cfg.sm_bonding = 0;
    ble_hs_cfg.sm_mitm = 0;                // MITM protection
    ble_hs_cfg.sm_sc = 0;                  // Secure Connections (BLE 4.2+)
    ble_hs_cfg.sm_our_key_dist = 0;
    ble_hs_cfg.sm_their_key_dist = 0;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_oob_data_flag = 0;       // no OOB

    // ── Bonding store (NVS) — tylko gdy bonding=1 ────────
    if (ble_hs_cfg.sm_bonding) {
        ble_store_config_init();
        ble_hs_cfg.store_read_cb = ble_store_config_read;
        ble_hs_cfg.store_write_cb = ble_store_config_write;
        ble_hs_cfg.store_delete_cb = ble_store_config_delete;
    }

    // ── Mandatory services (0x1800, 0x1801) ──────────────
    ble_svc_gap_init();  // ← DODANE — Generic Access
    ble_svc_gatt_init(); // ← DODANE — Generic Attribute + Service Changed

    // ── Custom service 0x00FF ─────────────────────────────
    ble_gatts_count_cfg(gatt_svcs);
    ble_gatts_add_svcs(gatt_svcs);
    ESP_LOGI(TAG, "Serwis 0x00FF zarejestrowany (DRO+CMD)");

    ble_hs_cfg.sync_cb = on_sync;

    nimble_port_freertos_init(host_task_fn);

    xTaskCreate(ble_notify_task, "ble_notify", 3072, NULL, 3, NULL);

    ESP_LOGI(TAG, "NimBLE host uruchomiony");
    return ESP_OK;
}