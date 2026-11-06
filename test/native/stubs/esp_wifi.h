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
inline esp_err_t esp_wifi_stop()
{
    stubEspNow().deepStalled = false;
    stubEspNow().wifiStops++;
    return ESP_OK;
}
inline esp_err_t esp_wifi_start() { return ESP_OK; }
inline esp_err_t esp_wifi_set_ps(int) { return ESP_OK; }
inline esp_err_t esp_wifi_set_max_tx_power(int) { return ESP_OK; }
// MAC esp_wifi_get_mac() reports; all zeros unless a test sets it.
inline uint8_t *stubWifiMac()
{
    static uint8_t mac[6] = {};
    return mac;
}
inline esp_err_t esp_wifi_get_mac(int, uint8_t mac[6])
{
    memcpy(mac, stubWifiMac(), 6);
    return ESP_OK;
}
