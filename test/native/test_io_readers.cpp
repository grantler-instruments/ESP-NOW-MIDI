// enomik::IO input readers: fake analog/digital inputs, as client_test does
// for the test wizard.

#include <catch2/catch_test_macros.hpp>

#include <map>

#include "arduino_stubs.h"
#include "client_host_stubs.h"
#include "enomik_client.h"

namespace {

uint8_t kDongle[6] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60};
constexpr uint8_t kPin = 9;

std::map<int, int> g_fake;
int readFake(uint8_t pin) { return g_fake[pin]; }

// Same mapping as examples/client_test: middle of the ADC range for a 7-bit level.
int adcForLevel(int level)
{
    if (level >= 127)
        return ADC_MAX_VALUE;
    return ((2 * level + 1) * ADC_MAX_VALUE) / 254;
}

void startClient(enomik::Client &client)
{
    REQUIRE(client.begin());
    REQUIRE(client.espnowMIDI.addPeer(kDongle));
    stubEspNowSent().clear();
}

void setPin(enomik::Client &client, uint8_t mode, MidiStatus type)
{
    enomik::PinConfig cfg(kPin, mode);
    cfg.midi_type = type;
    cfg.midi_channel = 1;
    cfg.midi_cc = 10;
    const auto sysex = enomik::SysExEncoder::encodePinConfig(cfg, enomik::SysExCommand::SET_PIN_CONFIG);
    client.io.onSysEx(sysex.data, sysex.length);
}

// Runs loop() until the pin sends a 3-byte MIDI message, returns its bytes.
std::vector<uint8_t> nextSent(enomik::Client &client)
{
    for (int i = 0; i < 20; i++)
    {
        stubEspNowSent().clear();
        millisNow() += 10;
        client.loop();
        for (const auto &p : stubEspNowSent())
            if (p.data.size() == 3)
                return p.data;
    }
    return {};
}

} // namespace

TEST_CASE("analog reader fakes every 7-bit level exactly", "[io][readers]")
{
    enomik::Client client;
    startClient(client);
    client.io.setAnalogReader(readFake);
    g_fake[kPin] = adcForLevel(0);
    setPin(client, enomik::ENOMIK_ANALOG_INPUT, MIDI_CONTROL_CHANGE);
    REQUIRE(nextSent(client) == std::vector<uint8_t>{0xB0, 10, 0});

    // Walk every level. 1 and 126 snap to the endpoints (ANALOG_ENDPOINT_SNAP).
    for (int level = 2; level <= 127; level++)
    {
        if (level == 126)
            continue;
        g_fake[kPin] = adcForLevel(level);
        std::vector<uint8_t> got;
        for (int i = 0; i < 20 && (got.empty() || got[2] != level); i++)
            got = nextSent(client);
        INFO("level " << level);
        REQUIRE(got == std::vector<uint8_t>{0xB0, 10, uint8_t(level)});
    }
}

TEST_CASE("analog levels next to the endpoints snap to them", "[io][readers]")
{
    enomik::Client client;
    startClient(client);
    client.io.setAnalogReader(readFake);
    g_fake[kPin] = adcForLevel(64);
    setPin(client, enomik::ENOMIK_ANALOG_INPUT, MIDI_CONTROL_CHANGE);
    REQUIRE(nextSent(client)[2] == 64);

    g_fake[kPin] = adcForLevel(1);
    std::vector<uint8_t> got;
    for (int i = 0; i < 20 && (got.empty() || got[2] > 1); i++)
        got = nextSent(client);
    REQUIRE(got[2] == 0);

    g_fake[kPin] = adcForLevel(126);
    got.clear();
    for (int i = 0; i < 20 && (got.empty() || got[2] < 126); i++)
        got = nextSent(client);
    REQUIRE(got[2] == 127);
}

TEST_CASE("analog reader drives a pitch-bend pin across its range", "[io][readers]")
{
    enomik::Client client;
    startClient(client);
    client.io.setAnalogReader(readFake);
    g_fake[kPin] = adcForLevel(0);
    setPin(client, enomik::ENOMIK_ANALOG_INPUT, MIDI_PITCH_BEND);

    auto raw = [](const std::vector<uint8_t> &d) { return (d[2] << 7) | d[1]; };
    REQUIRE(raw(nextSent(client)) == 0);

    g_fake[kPin] = adcForLevel(127);
    REQUIRE(raw(nextSent(client)) == 16383);

    g_fake[kPin] = adcForLevel(64);
    const int center = raw(nextSent(client)) - 8192;
    REQUIRE(center >= -256);
    REQUIRE(center <= 256);
}

TEST_CASE("digital reader fakes button presses", "[io][readers]")
{
    enomik::Client client;
    startClient(client);
    client.io.setDigitalReader(readFake);
    g_fake[kPin] = HIGH;
    setPin(client, enomik::ENOMIK_INPUT_PULLUP, MIDI_CONTROL_CHANGE);
    REQUIRE(nextSent(client) == std::vector<uint8_t>{0xB0, 10, 0});

    g_fake[kPin] = LOW;
    REQUIRE(nextSent(client) == std::vector<uint8_t>{0xB0, 10, 127});

    g_fake[kPin] = HIGH;
    REQUIRE(nextSent(client) == std::vector<uint8_t>{0xB0, 10, 0});
}

TEST_CASE("clearing the readers reads the hardware again", "[io][readers]")
{
    enomik::Client client;
    startClient(client);
    client.io.setAnalogReader(readFake);
    g_fake[kPin] = adcForLevel(100);
    stubAnalogLevels()[kPin] = 0;
    setPin(client, enomik::ENOMIK_ANALOG_INPUT, MIDI_CONTROL_CHANGE);
    REQUIRE(nextSent(client)[2] == 100);

    client.io.setAnalogReader(nullptr);
    std::vector<uint8_t> got;
    for (int i = 0; i < 20 && (got.empty() || got[2] != 0); i++)
        got = nextSent(client);
    REQUIRE(got == std::vector<uint8_t>{0xB0, 10, 0});
}
