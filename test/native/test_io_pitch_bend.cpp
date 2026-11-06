// enomik::IO pitch-bend pins: inputs send raw 14-bit wire values, outputs
// follow received (signed) pitch bend.

#include <catch2/catch_test_macros.hpp>

#include "arduino_stubs.h"
#include "client_host_stubs.h"
#include "enomik_client.h"

namespace {

uint8_t kDongle[6] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60};
constexpr uint8_t kPin = 7;

void setPin(enomik::Client &client, uint8_t mode, uint8_t minValue = 0, uint8_t maxValue = 127)
{
    enomik::PinConfig cfg(kPin, mode);
    cfg.midi_type = MIDI_PITCH_BEND;
    cfg.midi_channel = 1;
    cfg.min_midi_value = minValue;
    cfg.max_midi_value = maxValue;
    const auto sysex = enomik::SysExEncoder::encodePinConfig(cfg, enomik::SysExCommand::SET_PIN_CONFIG);
    client.io.onSysEx(sysex.data, sysex.length);
}

void startClient(enomik::Client &client)
{
    REQUIRE(client.begin()); // wires IO input pins to the client's send path
    REQUIRE(client.espnowMIDI.addPeer(kDongle));
    stubEspNowSent().clear();
}

// Raw 14-bit value of the last pitch bend sent over ESP-NOW.
int lastSentBend()
{
    REQUIRE_FALSE(stubEspNowSent().empty());
    const auto &data = stubEspNowSent().back().data;
    REQUIRE(data.size() == 3);
    REQUIRE(data[0] == 0xE0);
    return (data[2] << 7) | data[1];
}

int sendAnalog(int adc)
{
    enomik::Client client;
    startClient(client);
    setPin(client, enomik::ENOMIK_ANALOG_INPUT);
    stubAnalogLevels()[kPin] = adc;
    millisNow() += 100; // past the analog send rate limit
    client.loop();
    return lastSentBend();
}

void receiveBend(enomik::Client &client, int raw)
{
    const uint8_t packet[3] = {0xE0, uint8_t(raw & 0x7F), uint8_t(raw >> 7)};
    client.espnowMIDI.OnDataRecv(kDongle, packet, 3);
    client.loop();
}

} // namespace

TEST_CASE("analog pitch-bend input sends the full 14-bit range", "[io][pitchbend]")
{
    REQUIRE(sendAnalog(0) == 0);
    REQUIRE(sendAnalog(ADC_MAX_VALUE) == 16383);

    const int mid = sendAnalog(ADC_MAX_VALUE / 2);
    REQUIRE(mid > 8000);
    REQUIRE(mid < 8400);
}

TEST_CASE("digital pitch-bend input sends min/max scaled to 14 bits", "[io][pitchbend]")
{
    enomik::Client client;
    startClient(client);
    setPin(client, enomik::ENOMIK_INPUT_PULLUP, 64, 127);

    stubPinLevels()[kPin] = HIGH; // released
    client.loop();
    REQUIRE(lastSentBend() == 8192);

    stubPinLevels()[kPin] = LOW; // pressed
    millisNow() += 100;
    client.loop();
    millisNow() += 100;
    client.loop();
    REQUIRE(lastSentBend() == 16383);
}

TEST_CASE("received pitch bend drives output pins around center", "[io][pitchbend]")
{
    enomik::Client client;
    startClient(client);

    setPin(client, enomik::ENOMIK_OUTPUT);
    receiveBend(client, 0);
    REQUIRE(stubPinLevels()[kPin] == LOW);
    receiveBend(client, 8192);
    REQUIRE(stubPinLevels()[kPin] == LOW);
    receiveBend(client, 16383);
    REQUIRE(stubPinLevels()[kPin] == HIGH);

    setPin(client, enomik::ENOMIK_ANALOG_OUTPUT);
    receiveBend(client, 0);
    REQUIRE(stubPinLevels()[kPin] == 0);
    receiveBend(client, 8192);
    REQUIRE(stubPinLevels()[kPin] == PWM_MAX_VALUE / 2);
    receiveBend(client, 16383);
    REQUIRE(stubPinLevels()[kPin] == PWM_MAX_VALUE);
}
