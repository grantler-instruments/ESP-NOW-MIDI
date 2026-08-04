/**
 * Compile / smoke test for esp_now_midi under pure ESP-IDF.
 *
 * Build (from this directory, with IDF exported):
 *   idf.py set-target esp32s3
 *   idf.py build
 */
#include "esp_log.h"
#include "esp_now_midi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "esp_now_midi_idf_test";

static esp_now_midi g_midi;

static void onNoteOn(byte channel, byte note, byte velocity)
{
    ESP_LOGI(TAG, "Note On ch=%u note=%u vel=%u", channel, note, velocity);
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "starting esp_now_midi IDF smoke test");

    if (!g_midi.begin(/*reducePower*/ false, /*autoPeerDiscovery*/ true,
                      esp_now_midi::DefaultOnDataSent, /*manageWifi*/ true))
    {
        ESP_LOGE(TAG, "esp_now_midi.begin() failed");
        return;
    }

    g_midi.setHandleNoteOn(onNoteOn);
    ESP_LOGI(TAG, "begin OK, peers=%d", g_midi.getPeersCount());

    // Exercise send path (no peers yet — returns ESP_FAIL, but must link).
    esp_err_t err = g_midi.sendNoteOn(60, 100, 1);
    ESP_LOGI(TAG, "sendNoteOn (no peers) -> %s", esp_err_to_name(err));

    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
