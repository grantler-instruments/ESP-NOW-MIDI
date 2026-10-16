// enomik::IO output pins honor the configured min/max range, including an
// inverted one (min > max) as sent by the app to flip a pin.

#include <catch2/catch_test_macros.hpp>

#include "arduino_stubs.h"
#include "client_host_stubs.h"
#include "enomik_client.h"

namespace {

uint8_t kDongle[6] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60};
constexpr uint8_t kPin = 9;
constexpr uint8_t kNote = 60;
constexpr uint8_t kCC = 20;

void startClient(enomik::Client &client)
{
    REQUIRE(client.begin());
    REQUIRE(client.espnowMIDI.addPeer(kDongle));
}

void setPin(enomik::Client &client, uint8_t mode, MidiStatus type, uint8_t minValue, uint8_t maxValue)
{
    enomik::PinConfig cfg(kPin, mode);
    cfg.midi_type = type;
    cfg.midi_channel = 1;
    cfg.midi_note = kNote;
    cfg.midi_cc = kCC;
    cfg.min_midi_value = minValue;
    cfg.max_midi_value = maxValue;
    const auto sysex = enomik::SysExEncoder::encodePinConfig(cfg, enomik::SysExCommand::SET_PIN_CONFIG);
    client.io.onSysEx(sysex.data, sysex.length);
}

void receive(enomik::Client &client, uint8_t status, uint8_t data1, uint8_t data2)
{
    const uint8_t packet[3] = {status, data1, data2};
    client.espnowMIDI.OnDataRecv(kDongle, packet, 3);
    client.loop();
}

void receiveBend(enomik::Client &client, int raw)
{
    receive(client, 0xE0, uint8_t(raw & 0x7F), uint8_t(raw >> 7));
}

int level() { return stubPinLevels()[kPin]; }

} // namespace

TEST_CASE("digital output notes follow the range direction", "[io][output]")
{
    enomik::Client client;
    startClient(client);

    setPin(client, enomik::ENOMIK_OUTPUT, MIDI_NOTE_ON, 0, 127);
    REQUIRE(level() == LOW);
    receive(client, 0x90, kNote, 100);
    REQUIRE(level() == HIGH);
    receive(client, 0x80, kNote, 0);
    REQUIRE(level() == LOW);

    setPin(client, enomik::ENOMIK_OUTPUT, MIDI_NOTE_ON, 127, 0);
    REQUIRE(level() == HIGH); // idle = off = high
    receive(client, 0x90, kNote, 100);
    REQUIRE(level() == LOW);
    receive(client, 0x80, kNote, 0);
    REQUIRE(level() == HIGH);
    receive(client, 0x90, kNote, 100);
    receive(client, 0x90, kNote, 0); // Note On velocity 0 = Note Off
    REQUIRE(level() == HIGH);
}

TEST_CASE("analog output notes follow the range direction", "[io][output]")
{
    enomik::Client client;
    startClient(client);

    setPin(client, enomik::ENOMIK_ANALOG_OUTPUT, MIDI_NOTE_ON, 0, 127);
    REQUIRE(level() == 0);
    receive(client, 0x90, kNote, 127);
    REQUIRE(level() == PWM_MAX_VALUE);
    receive(client, 0x80, kNote, 0);
    REQUIRE(level() == 0);

    setPin(client, enomik::ENOMIK_ANALOG_OUTPUT, MIDI_NOTE_ON, 127, 0);
    REQUIRE(level() == PWM_MAX_VALUE);
    receive(client, 0x90, kNote, 127);
    REQUIRE(level() == 0);
    receive(client, 0x80, kNote, 0);
    REQUIRE(level() == PWM_MAX_VALUE);
}

TEST_CASE("digital output CC maps through min/max before the threshold", "[io][output]")
{
    enomik::Client client;
    startClient(client);

    setPin(client, enomik::ENOMIK_OUTPUT, MIDI_CONTROL_CHANGE, 0, 127);
    receive(client, 0xB0, kCC, 0);
    REQUIRE(level() == LOW);
    receive(client, 0xB0, kCC, 127);
    REQUIRE(level() == HIGH);

    setPin(client, enomik::ENOMIK_OUTPUT, MIDI_CONTROL_CHANGE, 127, 0);
    receive(client, 0xB0, kCC, 0);
    REQUIRE(level() == HIGH);
    receive(client, 0xB0, kCC, 127);
    REQUIRE(level() == LOW);

    // custom range: midpoint is 30, not 63
    setPin(client, enomik::ENOMIK_OUTPUT, MIDI_CONTROL_CHANGE, 20, 40);
    receive(client, 0xB0, kCC, 25);
    REQUIRE(level() == LOW);
    receive(client, 0xB0, kCC, 35);
    REQUIRE(level() == HIGH);
}

TEST_CASE("analog output CC follows the range direction", "[io][output]")
{
    enomik::Client client;
    startClient(client);

    setPin(client, enomik::ENOMIK_ANALOG_OUTPUT, MIDI_CONTROL_CHANGE, 127, 0);
    receive(client, 0xB0, kCC, 0);
    REQUIRE(level() == PWM_MAX_VALUE);
    receive(client, 0xB0, kCC, 127);
    REQUIRE(level() == 0);
}

TEST_CASE("digital output pitch bend switches past the middle of the range", "[io][output][pitchbend]")
{
    enomik::Client client;
    startClient(client);

    // inverted: mirror image, a downward bend switches on, rest stays off
    setPin(client, enomik::ENOMIK_OUTPUT, MIDI_PITCH_BEND, 127, 0);
    REQUIRE(level() == LOW);
    receiveBend(client, 8192);
    REQUIRE(level() == LOW);
    receiveBend(client, 16383);
    REQUIRE(level() == LOW);
    receiveBend(client, 0);
    REQUIRE(level() == HIGH);

    // upper half: on only past halfway up
    setPin(client, enomik::ENOMIK_OUTPUT, MIDI_PITCH_BEND, 64, 127);
    receiveBend(client, 8192);
    REQUIRE(level() == LOW);
    receiveBend(client, 11000);
    REQUIRE(level() == LOW);
    receiveBend(client, 14000);
    REQUIRE(level() == HIGH);
    receiveBend(client, 0);
    REQUIRE(level() == LOW);

    // downward only: on past halfway down
    setPin(client, enomik::ENOMIK_OUTPUT, MIDI_PITCH_BEND, 64, 0);
    receiveBend(client, 8192);
    REQUIRE(level() == LOW);
    receiveBend(client, 16383);
    REQUIRE(level() == LOW);
    receiveBend(client, 5000);
    REQUIRE(level() == LOW);
    receiveBend(client, 2000);
    REQUIRE(level() == HIGH);
}

TEST_CASE("analog output pitch bend follows the range", "[io][output][pitchbend]")
{
    enomik::Client client;
    startClient(client);

    setPin(client, enomik::ENOMIK_ANALOG_OUTPUT, MIDI_PITCH_BEND, 127, 0);
    receiveBend(client, 0);
    REQUIRE(level() == PWM_MAX_VALUE);
    receiveBend(client, 8192);
    REQUIRE(level() == PWM_MAX_VALUE / 2);
    receiveBend(client, 16383);
    REQUIRE(level() == 0);

    // upper half only: center = off
    setPin(client, enomik::ENOMIK_ANALOG_OUTPUT, MIDI_PITCH_BEND, 64, 127);
    receiveBend(client, 0);
    REQUIRE(level() == 0);
    receiveBend(client, 8192);
    REQUIRE(level() == 0);
    receiveBend(client, 16383);
    REQUIRE(level() == PWM_MAX_VALUE);

    // downward only
    setPin(client, enomik::ENOMIK_ANALOG_OUTPUT, MIDI_PITCH_BEND, 64, 0);
    receiveBend(client, 8192);
    REQUIRE(level() == 0);
    receiveBend(client, 0);
    REQUIRE(level() == PWM_MAX_VALUE);
}
