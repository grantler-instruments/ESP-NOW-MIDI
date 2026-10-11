#pragma once
// Host stub of the ESP-IDF Wi-Fi calls used by the library (native tests).
#include "esp_now.h"

#define WIFI_SECOND_CHAN_NONE 0
enum
{
    WIFI_PS_NONE,
    WIFI_PS_MIN_MODEM,
};
enum
{
    WIFI_IF_STA = 0,
};
inline esp_err_t esp_wifi_set_channel(int, int) { return ESP_OK; }
inline esp_err_t esp_wifi_set_ps(int) { return ESP_OK; }
inline esp_err_t esp_wifi_set_max_tx_power(int) { return ESP_OK; }
inline esp_err_t esp_wifi_get_mac(int, uint8_t mac[6])
{
    memset(mac, 0, 6);
    return ESP_OK;
}
