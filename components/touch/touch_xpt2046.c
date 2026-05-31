// touch_xpt2046.c – XPT2046 resistive touch controller (SPI)
//
// UWAGA: Nie włączaj jednocześnie CONFIG_TOUCH_TYPE_XPT2046 (ten komponent)
//        i CONFIG_XPT2046_ENABLE_* (biblioteka ili9340). Będzie konflikt SPI!
#if defined(CONFIG_XPT2046_ENABLE_SAME_BUS) || defined(CONFIG_XPT2046_ENABLE_DIFF_BUS)
#error "Konflikt: wylacz XPT2046 w bibliotece ili9340 (CONFIG_XPT2046_DISABLE=y) przed uzyciem tego komponentu"
#endif

#include "touch.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "XPT2046";

// Default pins — override via menuconfig (Kconfig)
#ifndef CONFIG_TOUCH_XPT2046_CS
#define CONFIG_TOUCH_XPT2046_CS   GPIO_NUM_21
#endif
#ifndef CONFIG_TOUCH_XPT2046_IRQ
#define CONFIG_TOUCH_XPT2046_IRQ  GPIO_NUM_NC
#endif

// SPI device handle (shared bus with TFT on SPI2)
static spi_device_handle_t s_spi = NULL;

// ── Internal helpers ───────────────────────────────────────

// Send command byte, read 2 bytes result
static uint16_t xpt2046_read_cmd(uint8_t cmd)
{
    uint8_t tx[3] = { cmd, 0, 0 };
    uint8_t rx[3] = { 0 };

    spi_transaction_t t = {
        .length    = 24,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    if (spi_device_transmit(s_spi, &t) == ESP_OK) {
        // XPT2046 returns 12-bit value: bits 15..4 of 16-bit result
        uint16_t val = ((uint16_t)rx[1] << 8) | rx[2];
        return val >> 3;  // 12-bit → top bits
    }
    return 0;
}

// ── Public API ─────────────────────────────────────────────

esp_err_t touch_xpt2046_init(void)
{
    ESP_LOGI(TAG, "Init XPT2046 (CS=%d)", CONFIG_TOUCH_XPT2046_CS);

    // Add device to existing SPI2 bus (shared with TFT)
    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = 2 * 1000 * 1000,  // 2 MHz (XPT2046 max)
        .mode           = 0,                 // SPI mode 0
        .spics_io_num   = CONFIG_TOUCH_XPT2046_CS,
        .queue_size     = 3,
        .flags          = SPI_DEVICE_HALFDUPLEX,
    };

    esp_err_t ret = spi_bus_add_device(SPI2_HOST, &dev_cfg, &s_spi);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_add_device failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Pull CS high (inactive)
    gpio_set_direction(CONFIG_TOUCH_XPT2046_CS, GPIO_MODE_OUTPUT);
    gpio_set_level(CONFIG_TOUCH_XPT2046_CS, 1);

    // Optional IRQ pin (touch detect)
    if (CONFIG_TOUCH_XPT2046_IRQ != GPIO_NUM_NC) {
        gpio_set_direction(CONFIG_TOUCH_XPT2046_IRQ, GPIO_MODE_INPUT);
        gpio_set_pull_mode(CONFIG_TOUCH_XPT2046_IRQ, GPIO_PULLUP_ONLY);
    }

    ESP_LOGI(TAG, "XPT2046 ready");
    return ESP_OK;
}

esp_err_t touch_xpt2046_read_raw(touch_raw_t *out)
{
    if (!out || !s_spi) return ESP_ERR_INVALID_STATE;

    // XPT2046: read X (0xD0), Y (0x90), Z1 (0xB0), Z2 (0xC0)
    uint16_t x  = xpt2046_read_cmd(0xD0);  // X position
    uint16_t y  = xpt2046_read_cmd(0x90);  // Y position
    uint16_t z1 = xpt2046_read_cmd(0xB0);  // Z1 pressure
    uint16_t z2 = xpt2046_read_cmd(0xC0);  // Z2 pressure

    // Pressure: z = z1 - z2 (positive when touched)
    int32_t z_raw = (int32_t)z1 - (int32_t)z2;

    out->x = x;
    out->y = y;
    out->z = (z_raw > 0) ? (uint16_t)z_raw : 0;

    return ESP_OK;
}
