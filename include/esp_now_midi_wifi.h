/**
 * @file esp_now_midi_wifi.h
 * @brief Wi-Fi STA bring-up backends for Arduino and ESP-IDF.
 */
#pragma once

#include "./esp_now_midi_log.h"

#ifdef ARDUINO
#include <WiFi.h>
#elif defined(ESP_PLATFORM)
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#endif

namespace esp_now_midi_wifi
{

/**
 * @brief Ensures Wi-Fi is in STA mode and started.
 *
 * Call only when the library owns Wi-Fi (`manageWifi == true`).
 * Arduino uses `WiFi`; pure ESP-IDF uses `esp_wifi` / netif.
 * Host / native tests: no-op success.
 *
 * @return `true` when STA is ready (or already was).
 */
inline bool ensureWifiSta()
{
#ifdef ARDUINO
  if (WiFi.getMode() != WIFI_MODE_STA)
  {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);
  }
  return true;
#elif defined(ESP_PLATFORM)
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
  {
    nvs_flash_erase();
    err = nvs_flash_init();
  }
  if (err != ESP_OK)
  {
    EspNowMidiLog::e("nvs_flash_init failed: %d", static_cast<int>(err));
    return false;
  }

  err = esp_netif_init();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
  {
    EspNowMidiLog::e("esp_netif_init failed: %d", static_cast<int>(err));
    return false;
  }

  err = esp_event_loop_create_default();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
  {
    EspNowMidiLog::e("esp_event_loop_create_default failed: %d", static_cast<int>(err));
    return false;
  }

  wifi_mode_t mode = WIFI_MODE_NULL;
  err = esp_wifi_get_mode(&mode);
  if (err == ESP_ERR_WIFI_NOT_INIT)
  {
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK)
    {
      EspNowMidiLog::e("esp_wifi_init failed: %d", static_cast<int>(err));
      return false;
    }
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK)
    {
      EspNowMidiLog::e("esp_wifi_set_mode failed: %d", static_cast<int>(err));
      return false;
    }
    err = esp_wifi_start();
    if (err != ESP_OK)
    {
      EspNowMidiLog::e("esp_wifi_start failed: %d", static_cast<int>(err));
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    return true;
  }

  if (err != ESP_OK)
  {
    EspNowMidiLog::e("esp_wifi_get_mode failed: %d", static_cast<int>(err));
    return false;
  }

  if (mode != WIFI_MODE_STA && mode != WIFI_MODE_APSTA)
  {
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK)
    {
      EspNowMidiLog::e("esp_wifi_set_mode failed: %d", static_cast<int>(err));
      return false;
    }
  }
  return true;
#else
  return true;
#endif
}

} // namespace esp_now_midi_wifi
