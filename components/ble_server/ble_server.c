// ble_server.c – BLE GATT server: DRO telemetry + JSON commands
#include "ble_server.h"
#include "axis.h"
#include "stepper.h"
#include "spindle.h"
#include "homing_state.h"

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_gatt_common_api.h"
#include "esp_log.h"

static const char *TAG = "BLE";

// ── UUIDs ──────────────────────────────────────────────────
#define GATTS_SERVICE_UUID       0x00FF
#define GATTS_CHAR_DRO_UUID      0xFF01   // read + notify: JSON status
#define GATTS_CHAR_CMD_UUID      0xFF02   // write: JSON command
#define GATTS_NUM_HANDLES        8

#define DEVICE_NAME              "MiniLathe"
#define BLE_APPEARANCE           0x0540  // Generic Machine Control

// ── GATT handles ───────────────────────────────────────────
static uint16_t s_service_handle  = 0;
static uint16_t s_char_dro_handle = 0;
static uint16_t s_char_cmd_handle = 0;
static uint16_t s_conn_id         = 0;
static bool     s_connected       = false;
static bool     s_notify_enabled  = false;

static TaskHandle_t s_notify_task = NULL;

// ── Build DRO JSON ─────────────────────────────────────────
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
        (sp.state >= SPINDLE_STATE_RAMPING_UP && sp.state <= SPINDLE_STATE_RUNNING) ? 1 : 0);
}

// ── Parse JSON command ─────────────────────────────────────
// Obsługuje: {"cmd":"jog","axis":"Z","steps":10}
//            {"cmd":"jog","axis":"X","steps":-5}
//            {"cmd":"spindle","rpm":60,"dir":"fwd"}
//            {"cmd":"spindle","rpm":0}
//            {"cmd":"estop"}
static void exec_ble_command(const char *json, int len)
{
    // Minimal JSON parser — szuka kluczy "cmd", "axis", "steps", "rpm", "dir"
    char cmd[16] = "", axis[4] = "Z", dir[4] = "fwd";
    int steps = 1, rpm = 0;

    // Wyciągnij "cmd"
    const char *p = strstr(json, "\"cmd\"");
    if (p) {
        p = strchr(p, ':');
        if (p) {
            p++; while (*p == ' ' || *p == '"') p++;
            int i = 0;
            while (*p && *p != '"' && *p != ',' && *p != '}' && i < 15) cmd[i++] = *p++;
            cmd[i] = '\0';
        }
    }

    // Wyciągnij "axis"
    p = strstr(json, "\"axis\"");
    if (p) {
        p = strchr(p, ':');
        if (p) {
            p++; while (*p == ' ' || *p == '"') p++;
            if (*p) { axis[0] = *p; axis[1] = '\0'; }
        }
    }

    // Wyciągnij "steps"
    p = strstr(json, "\"steps\"");
    if (p) {
        p = strchr(p, ':');
        if (p) steps = atoi(p + 1);
    }

    // Wyciągnij "rpm"
    p = strstr(json, "\"rpm\"");
    if (p) {
        p = strchr(p, ':');
        if (p) rpm = atoi(p + 1);
    }

    // Wyciągnij "dir"
    p = strstr(json, "\"dir\"");
    if (p) {
        p = strchr(p, ':');
        if (p) {
            p++; while (*p == ' ' || *p == '"') p++;
            int i = 0;
            while (*p && *p != '"' && *p != ',' && i < 3) dir[i++] = *p++;
            dir[i] = '\0';
        }
    }

    ESP_LOGI(TAG, "CMD: %s axis=%s steps=%d rpm=%d", cmd, axis, steps, rpm);

    if (strcmp(cmd, "estop") == 0) {
        spindle_emergency_stop();
        stepper_stop();
        if (g_axis_x) axis_stop(g_axis_x);
    }
    else if (strcmp(cmd, "jog") == 0) {
        if (steps == 0) steps = 1;
        if (steps > 500) steps = 500;
        axis_dir_t adir = (steps > 0) ? AXIS_DIR_POS : AXIS_DIR_NEG;
        uint16_t abs_steps = (uint16_t)(steps > 0 ? steps : -steps);

        if (axis[0] == 'X' || axis[0] == 'x') {
            if (g_axis_x && axis_get_state(g_axis_x) == AXIS_STATE_IDLE)
                axis_jog(g_axis_x, adir, abs_steps, 40);
        } else {
            if (stepper_get_state() == STEPPER_STATE_IDLE)
                axis_jog(g_axis_z, adir, abs_steps, 40);
        }
    }
    else if (strcmp(cmd, "spindle") == 0) {
        if (rpm > 0) {
            spindle_dir_t sdir = (dir[0] == 'r' || dir[0] == 'R')
                               ? SPINDLE_DIR_REV : SPINDLE_DIR_FWD;
            spindle_start((uint16_t)rpm, sdir);
        } else {
            spindle_stop();
        }
    }
}

// ── Notify task: co 300ms wysyła DRO jeśli klient podłączony ──
static void ble_notify_task(void *arg)
{
    char buf[256];
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(300));
        if (!s_connected || !s_notify_enabled || s_char_dro_handle == 0) continue;

        build_dro_json(buf, sizeof(buf));
        esp_ble_gatts_send_indicate(
            s_service_handle >> 8,  // gatts_if from app_id (simplified: use default)
            s_conn_id, s_char_dro_handle, strlen(buf), (uint8_t *)buf, false);
    }
}

// ── Forward declarations ──────────────────────────────────
static void ble_start_advertising(void);

// ── GATT event handler ─────────────────────────────────────
static void gatts_event_handler(esp_gatts_cb_event_t event,
                                esp_gatt_if_t gatts_if,
                                esp_ble_gatts_cb_param_t *param)
{
    switch (event) {
    case ESP_GATTS_REG_EVT: {
        ESP_LOGI(TAG, "GATT server registered");
        // Create service
        esp_gatt_srvc_id_t svc = {
            .is_primary = true,
            .id = {
                .inst_id = 0,
                .uuid = {
                    .len = ESP_UUID_LEN_16,
                    .uuid = {.uuid16 = GATTS_SERVICE_UUID},
                },
            },
        };
        esp_ble_gatts_create_service(gatts_if, &svc, GATTS_NUM_HANDLES);
        break;
    }

    case ESP_GATTS_CREATE_EVT: {
        s_service_handle = param->create.service_handle;
        ESP_LOGI(TAG, "Service created, handle=%d", s_service_handle);

        // Characteristic: DRO (read + notify)
        esp_bt_uuid_t dro_uuid = {.len = ESP_UUID_LEN_16, .uuid = {.uuid16 = GATTS_CHAR_DRO_UUID}};
        esp_ble_gatts_add_char(
            s_service_handle, &dro_uuid,
            ESP_GATT_PERM_READ,
            ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY,
            NULL, NULL);

        // Characteristic: CMD (write)
        esp_bt_uuid_t cmd_uuid = {.len = ESP_UUID_LEN_16, .uuid = {.uuid16 = GATTS_CHAR_CMD_UUID}};
        esp_ble_gatts_add_char(
            s_service_handle, &cmd_uuid,
            ESP_GATT_PERM_WRITE,
            ESP_GATT_CHAR_PROP_BIT_WRITE,
            NULL, NULL);
        break;
    }

    case ESP_GATTS_ADD_CHAR_EVT: {
        uint16_t uuid16 = param->add_char.char_uuid.uuid.uuid16;
        if (uuid16 == GATTS_CHAR_DRO_UUID) {
            s_char_dro_handle = param->add_char.attr_handle;
            ESP_LOGI(TAG, "DRO char handle=%d", s_char_dro_handle);
        } else if (uuid16 == GATTS_CHAR_CMD_UUID) {
            s_char_cmd_handle = param->add_char.attr_handle;
            ESP_LOGI(TAG, "CMD char handle=%d", s_char_cmd_handle);
            esp_ble_gatts_start_service(s_service_handle);
        }
        break;
    }

    case ESP_GATTS_CONNECT_EVT: {
        s_conn_id = param->connect.conn_id;
        s_connected = true;
        ESP_LOGI(TAG, "Klient BLE podlaczony, conn=%d", s_conn_id);
        ble_start_advertising();
        break;
    }

    case ESP_GATTS_DISCONNECT_EVT: {
        s_connected = false;
        s_notify_enabled = false;
        ESP_LOGI(TAG, "Klient BLE rozlaczony");
        ble_start_advertising();
        break;
    }

    case ESP_GATTS_READ_EVT: {
        if (param->read.handle == s_char_dro_handle) {
            char buf[256];
            build_dro_json(buf, sizeof(buf));
            esp_gatt_rsp_t rsp = {0};
            rsp.attr_value.handle = s_char_dro_handle;
            rsp.attr_value.len = strlen(buf);
            memcpy(rsp.attr_value.value, buf, rsp.attr_value.len);
            esp_ble_gatts_send_response(gatts_if, param->read.conn_id,
                                        param->read.trans_id, ESP_GATT_OK, &rsp);
        }
        break;
    }

    case ESP_GATTS_WRITE_EVT: {
        if (param->write.handle == s_char_cmd_handle && param->write.len > 0) {
            char buf[128] = {0};
            int len = param->write.len;
            if (len > 127) len = 127;
            memcpy(buf, param->write.value, len);
            exec_ble_command(buf, len);

            if (param->write.need_rsp) {
                esp_ble_gatts_send_response(gatts_if, param->write.conn_id,
                    param->write.trans_id, ESP_GATT_OK, NULL);
            }
        }
        break;
    }

    case ESP_GATTS_MTU_EVT:
        ESP_LOGI(TAG, "MTU=%d", param->mtu.mtu);
        break;

    case ESP_GATTS_START_EVT:
        ESP_LOGI(TAG, "Service started, advertising...");
        break;

    default:
        break;
    }
}

// ── Start advertising (no-op — esp_ble_gap_start_advertising not linked) ──
// BLE GATT server is running. Connect using MAC: 44:1b:f6:d3:71:6e (visible in boot log).
static void ble_start_advertising(void)
{
    ESP_LOGI(TAG, "BLE GATT ready (advertising skipped — connect by MAC)");
}

// ── GAP event handler ──────────────────────────────────────
static void gap_event_handler(esp_gap_ble_cb_event_t event,
                              esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        ble_start_advertising();
        break;
    default:
        break;
    }
}

// ── Init ───────────────────────────────────────────────────
esp_err_t ble_server_init(void)
{
    ESP_LOGI(TAG, "Inicjalizacja BLE...");

    // Init BT controller
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_err_t ret = esp_bt_controller_init(&bt_cfg);
    if (ret) {
        ESP_LOGE(TAG, "BT controller init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) {
        ESP_LOGE(TAG, "BT controller enable failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bluedroid_init();
    if (ret) {
        ESP_LOGE(TAG, "Bluedroid init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = esp_bluedroid_enable();
    if (ret) {
        ESP_LOGE(TAG, "Bluedroid enable failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Register callbacks
    esp_ble_gatts_register_callback(gatts_event_handler);
    esp_ble_gap_register_callback(gap_event_handler);
    esp_ble_gatts_app_register(0);  // app_id = 0

    // Set device name (no config_adv_data — uses defaults)
    esp_ble_gap_set_device_name(DEVICE_NAME);

    // Start notify task
    xTaskCreate(ble_notify_task, "ble_notify", 3072, NULL, 3, &s_notify_task);

    ESP_LOGI(TAG, "BLE GATT server gotowy jako '%s'", DEVICE_NAME);
    return ESP_OK;
}
