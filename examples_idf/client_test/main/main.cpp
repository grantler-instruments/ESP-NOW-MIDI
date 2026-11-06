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
static const byte kFakeChannel = TEST_FAKE_CHANNEL;
static const byte kApiTriggerChannel = TEST_API_TRIGGER_CHANNEL;
static const byte kApiChannel = TEST_API_CHANNEL;

// --- Fake inputs (see examples/client_test/client_test.ino) -------------------
static const int kNotFaked = -1;
static int g_fakeAnalog[TEST_FAKE_PIN_COUNT];
static int g_fakeDigital[TEST_FAKE_PIN_COUNT];

static void clearFakes()
{
    for (int i = 0; i < TEST_FAKE_PIN_COUNT; i++)
    {
        g_fakeAnalog[i] = kNotFaked;
        g_fakeDigital[i] = kNotFaked;
    }
}

// ADC value in the middle of the range that maps back to this 7-bit level.
static int adcForLevel(int level)
{
    if (level >= 127)
    {
        return ADC_MAX_VALUE;
    }
    return ((2 * level + 1) * ADC_MAX_VALUE) / 254;
}

static int readAnalog(uint8_t pin)
{
    if (pin < TEST_FAKE_PIN_COUNT && g_fakeAnalog[pin] != kNotFaked)
    {
        return g_fakeAnalog[pin];
    }
    return analogRead(pin);
}

static int readDigital(uint8_t pin)
{
    if (pin < TEST_FAKE_PIN_COUNT && g_fakeDigital[pin] != kNotFaked)
    {
        return g_fakeDigital[pin];
    }
    return digitalRead(pin);
}

static void setFakeDigital(byte pin, bool pressed)
{
    if (pin < TEST_FAKE_PIN_COUNT)
    {
        g_fakeDigital[pin] = pressed ? LOW : HIGH;
    }
}

// --- API sends (keep in sync with API_CASES in scripts/wizard/cases.py) ---------
static const uint8_t kSysExShort[] = {0xF0, 0x7D, 0x7F, 0x7F, 0x01, 0x02, 0x03, 0xF7};
static const uint8_t kSysExLong[] = {0xF0, 0x7D, 0x7F, 0x7F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
                                     0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0xF7};

// More than one ESP-NOW frame (240-byte payloads): F0 7D 7F 7F, 0x00..0x7F repeating, F7.
static const uint16_t kSysExFragmentedLength = 300;

static void sendFragmentedSysEx()
{
    static uint8_t sysex[kSysExFragmentedLength];
    sysex[0] = 0xF0;
    sysex[1] = 0x7D;
    sysex[2] = 0x7F;
    sysex[3] = 0x7F;
    for (uint16_t i = 0; i < kSysExFragmentedLength - 5; i++)
    {
        sysex[4 + i] = i & 0x7F;
    }
    sysex[kSysExFragmentedLength - 1] = 0xF7;
    g_client.sendSysEx(sysex, kSysExFragmentedLength);
}

static void runApiCase(byte index)
{
    switch (index)
    {
    case 0: g_client.sendNoteOn(60, 100, kApiChannel); break;
    case 1: g_client.sendNoteOff(60, 0, kApiChannel); break;
    case 2: g_client.sendControlChange(20, 64, kApiChannel); break;
    case 3: g_client.sendProgramChange(7, kApiChannel); break;
    case 4: g_client.sendPitchBend(-8192, kApiChannel); break;
    case 5: g_client.sendPitchBend(0, kApiChannel); break;
    case 6: g_client.sendPitchBend(8191, kApiChannel); break;
    case 7: g_client.sendAfterTouch(50, kApiChannel); break;
    case 8: g_client.sendPolyAfterTouch(60, 70, kApiChannel); break;
    case 9: g_client.sendClock(); break;
    case 10: g_client.sendStart(); break;
    case 11: g_client.sendContinue(); break;
    case 12: g_client.sendStop(); break;
    case 13: g_client.sendSongPosition(1234); break;
    case 14: g_client.sendSongSelect(3); break;
    case 15: g_client.sendSysEx(kSysExShort, sizeof(kSysExShort)); break;
    case 16: g_client.sendSysEx(kSysExLong, sizeof(kSysExLong)); break;
    case 17: sendFragmentedSysEx(); break;
    default: break;
    }
}

static void onNoteOn(byte channel, byte note, byte velocity)
{
    if (channel == kFakeChannel)
    {
        setFakeDigital(note, velocity > 0);
    }
    if (channel == kTestChannel)
    {
        g_client.sendNoteOn(note, velocity, channel);
    }
}

static void onNoteOff(byte channel, byte note, byte velocity)
{
    if (channel == kFakeChannel)
    {
        setFakeDigital(note, false);
    }
    if (channel == kTestChannel)
    {
        g_client.sendNoteOff(note, velocity, channel);
    }
}

static void onControlChange(byte channel, byte control, byte value)
{
    if (channel == kFakeChannel && control < TEST_FAKE_PIN_COUNT)
    {
        g_fakeAnalog[control] = adcForLevel(value);
    }
    if (channel == kTestChannel)
    {
        g_client.sendControlChange(control, value, channel);
    }
}

static void onProgramChange(byte channel, byte program)
{
    if (channel == kFakeChannel)
    {
        clearFakes();
    }
    if (channel == kApiTriggerChannel)
    {
        runApiCase(program);
    }
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

// System messages: echoed regardless of channel.
static void onClock() { g_client.sendClock(); }
static void onStart() { g_client.sendStart(); }
static void onContinue() { g_client.sendContinue(); }
static void onStop() { g_client.sendStop(); }
static void onSongPosition(uint16_t position) { g_client.sendSongPosition(position); }
static void onSongSelect(byte song) { g_client.sendSongSelect(song); }

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "starting enomik::Client test DUT");

    clearFakes();

    g_client.setName("idf_client_test"); // optional
    g_client.setHeartbeatInterval(TEST_HEARTBEAT_INTERVAL_MS);
    if (!g_client.begin())
    {
        ESP_LOGE(TAG, "enomik::Client::begin() failed");
        return;
    }
    g_client.io.setAnalogReader(readAnalog);
    g_client.io.setDigitalReader(readDigital);

    g_client.setHandleNoteOn(onNoteOn);
    g_client.setHandleNoteOff(onNoteOff);
    g_client.setHandleControlChange(onControlChange);
    g_client.setHandleProgramChange(onProgramChange);
    g_client.setHandlePitchBend(onPitchBend);
    g_client.setHandleAfterTouchChannel(onAfterTouch);
    g_client.setHandleAfterTouchPoly(onPolyAfterTouch);
    g_client.setHandleClock(onClock);
    g_client.setHandleStart(onStart);
    g_client.setHandleContinue(onContinue);
    g_client.setHandleStop(onStop);
    g_client.setHandleSongPosition(onSongPosition);
    g_client.setHandleSongSelect(onSongSelect);

    ESP_LOGI(TAG, "begin OK, echo_ch=%d peers=%d",
             static_cast<int>(kTestChannel), g_client.getPeerCount());

    while (true)
    {
        g_client.loop();
        vTaskDelay(1);
    }
}
