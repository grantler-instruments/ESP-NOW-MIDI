/**
 * ESP-NOW MIDI client, pure ESP-IDF port of examples/client.
 *
 *   idf.py set-target esp32s2   # or esp32 / esp32s3
 *   idf.py build
 */
#include "esp_log.h"
#include "enomik_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "enomik_client_idf_example";

// on the dongle: run the print_mac firmware and paste it here
// uint8_t peerMacAddress[6] = { 0xCC, 0x8D, 0xA2, 0x8B, 0x85, 0x1C };

static enomik::Client g_client;

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "starting enomik::Client IDF example");

    if (!g_client.begin())
    {
        ESP_LOGE(TAG, "enomik::Client::begin() failed");
        return;
    }

    // g_client.addPeer(peerMacAddress);

    ESP_LOGI(TAG, "begin OK, peers=%d", g_client.getPeerCount());

    while (true)
    {
        g_client.loop();
        vTaskDelay(1);
    }
}
