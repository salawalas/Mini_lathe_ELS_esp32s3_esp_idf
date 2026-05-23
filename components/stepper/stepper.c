#include "stepper.h"
#include "esp_log.h"

static const char *TAG = "STEPPER";

void stepper_set_microstep(uint16_t s)
{
    (void)s;
    ESP_LOGW(TAG, "Mikrokrok ustawia sie DIP-switchami na DM556 – zmiana w UI nie ma efektu");
}
