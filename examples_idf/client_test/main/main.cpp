/**
 * client_test — deterministic DUT for scripts/wizard.
 * Pure ESP-IDF port of examples/client_test.
 *
 *   idf.py set-target esp32s2   # or esp32s3
 *   idf.py build
 */
#include "config.h"
#include "esp_log.h"
#include "enomik_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "enomik_client_test_idf";

static enomik::Client g_client;
static const byte kTestChannel = TEST_ECHO_CHANNEL;
static unsigned long g_lastHandshakeMs = 0;

static void onNoteOn(byte channel, byte note, byte velocity)
{
    if (channel == kTestChannel)
    {
        g_client.sendNoteOn(note, velocity, channel);
    }
}

static void onNoteOff(byte channel, byte note, byte velocity)
{
    if (channel == kTestChannel)
    {
        g_client.sendNoteOff(note, velocity, channel);
    }
}

static void onControlChange(byte channel, byte control, byte value)
{
    if (channel == kTestChannel)
    {
        g_client.sendControlChange(control, value, channel);
    }
}

static void onProgramChange(byte channel, byte program)
{
    if (channel == kTestChannel)
    {
        g_client.sendProgramChange(program, channel);
    }
}

static void onPitchBend(byte channel, int value)
{
    if (channel == kTestChannel)
    {
        g_client.sendPitchBend(value, channel);
    }
}

static void onAfterTouch(byte channel, byte pressure)
{
    if (channel == kTestChannel)
    {
        g_client.sendAfterTouch(pressure, channel);
    }
}

static void onPolyAfterTouch(byte channel, byte note, byte pressure)
{
    if (channel == kTestChannel)
    {
        g_client.sendPolyAfterTouch(note, pressure, channel);
    }
}

static void maybeHandshake()
{
    const unsigned long now = millis();
    if (now - g_lastHandshakeMs < TEST_HANDSHAKE_INTERVAL_MS)
    {
        return;
    }
    g_lastHandshakeMs = now;

    // Only meaningful once we know at least one peer (the dongle).
    if (g_client.getPeerCount() > 0)
    {
        g_client.sendControlChange(127, 127, 16);
    }
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "starting enomik::Client test DUT");

    if (!g_client.begin())
    {
        ESP_LOGE(TAG, "enomik::Client::begin() failed");
        return;
    }

    g_client.setHandleNoteOn(onNoteOn);
    g_client.setHandleNoteOff(onNoteOff);
    g_client.setHandleControlChange(onControlChange);
    g_client.setHandleProgramChange(onProgramChange);
    g_client.setHandlePitchBend(onPitchBend);
    g_client.setHandleAfterTouchChannel(onAfterTouch);
    g_client.setHandleAfterTouchPoly(onPolyAfterTouch);

    ESP_LOGI(TAG, "begin OK, echo_ch=%d peers=%d",
             static_cast<int>(kTestChannel), g_client.getPeerCount());

    while (true)
    {
        g_client.loop();
        maybeHandshake();
        vTaskDelay(1);
    }
}
