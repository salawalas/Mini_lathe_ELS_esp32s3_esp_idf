#include "stepper.h"
#include "esp_log.h"

static const char *TAG = "STEPPER";

void stepper_set_microstep(uint16_t s)
{
    (void)s;
    // UWAGA: Mikrokrok sterowników DM556 ustawia się fizycznymi DIP-switchami (SW5-SW8).
    // Funkcja istnieje dla zgodności API – wywołanie przez UI (Settings) loguje
    // przypomnienie. Aby zmienić mikrostepping, przełącz DIP na DM556:
    //   SW5 SW6 SW7 SW8 => 12800 kr/obr (domyślnie)
    //   Patrz README.md → Parametry DM556
    ESP_LOGW(TAG, "Mikrokrok: ustaw DIP-switchami na DM556 (UI nie ma efektu)");
}
