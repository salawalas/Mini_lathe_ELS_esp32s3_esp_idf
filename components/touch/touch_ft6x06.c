// touch_ft6x06.c – FT6x06 capacitive touch controller (I2C)
// Stub — do zaimplementowania przy pierwszym panelu pojemnościowym.
#include "touch.h"
#include "esp_log.h"

static const char *TAG = "FT6X06";

esp_err_t touch_ft6x06_init(void)
{
    ESP_LOGW(TAG, "FT6x06 not implemented — stub");
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t touch_ft6x06_read_raw(touch_raw_t *out)
{
    (void)out;
    return ESP_ERR_NOT_SUPPORTED;
}
