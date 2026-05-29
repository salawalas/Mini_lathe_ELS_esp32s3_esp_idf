#pragma once
#include "esp_err.h"

// Start BLE GATT server. Returns ESP_OK on success.
esp_err_t ble_server_init(void);
