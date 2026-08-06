/**
 * USB MIDI <-> ESP-NOW dongle, pure ESP-IDF port of examples/dongle.
 *
 * Requires a native-USB target (ESP32-S2 / ESP32-S3):
 *   idf.py set-target esp32s2
 *   idf.py build
 *
 * Optional SSD1306 status display (Lolin S2 Mini defaults: SDA=33, SCL=35).
 * Override with -DOLED_SDA_GPIO=… / -DOLED_SCL_GPIO=… if your board differs.
 */
#include "esp_log.h"
#include "enomik_dongle.h"
#include "ssd1306_display.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "enomik_dongle_idf_example";

static enomik::Dongle g_dongle;
static SSD1306Display g_display;

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "starting enomik::Dongle IDF example");

    g_dongle.setDisplay(&g_display);

    // Optional: customize USB identity before begin().
    // g_dongle.setManufacturerDescriptor("grantler instruments");
    // g_dongle.setProductDescriptor("enomik3000_dongle");

    // Optional: hard-add a known peer (or wait for auto-discovery).
    // uint8_t clientMac[6] = {0x84, 0xF7, 0x03, 0xF2, 0x54, 0x62};
    // g_dongle.addPeer(clientMac);

    // Optional bridge filters - see examples/dongle/dongle.ino for the full
    // set of examples (drop a channel, force a channel, drop sustain, ...).
    // g_dongle.setToHostFilter([](midi_message &msg) {
    //     return msg.channel != 10;
    // });

    if (!g_dongle.begin())
    {
        ESP_LOGE(TAG, "enomik::Dongle::begin() failed - requires ESP32-S2/S3 native USB");
        return;
    }

    ESP_LOGI(TAG, "begin OK, mac=%s peers=%d",
             g_dongle.getMacAddress().c_str(), g_dongle.getPeersCount());

    while (true)
    {
        g_dongle.loop();
        vTaskDelay(1);
    }
}
