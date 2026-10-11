// enomik::Client receive dispatch: ESP-NOW MIDI is handled in loop() by
// default, or immediately in the receive callback with setDispatchInLoop(false).

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <string>
#include <vector>
#include <cstring>
#include <thread>

#include "arduino_stubs.h"
#include "client_host_stubs.h"
#include "enomik_client.h"


namespace {

uint8_t kDongle[6] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60};
const uint8_t kNoteOn[3] = {0x90, 60, 100};
std::atomic<int> g_noteOns{0};

void countNoteOn(byte, byte, byte) { g_noteOns++; }

void setOutputPin(enomik::Client &client, uint8_t pin, uint8_t note)
{
    enomik::PinConfig cfg(pin, enomik::ENOMIK_OUTPUT);
    cfg.midi_type = MIDI_NOTE_ON;
    cfg.midi_channel = 1;
    cfg.midi_note = note;
    const auto sysex = enomik::SysExEncoder::encodePinConfig(cfg, enomik::SysExCommand::SET_PIN_CONFIG);
    client.io.onSysEx(sysex.data, sysex.length);
}

} // namespace

TEST_CASE("client runs ESP-NOW handlers in loop() by default", "[client][dispatch]")
{
    enomik::Client client;
    client.io.begin(); // as Client::begin() does: loads pins, enables SysEx config
    REQUIRE(client.espnowMIDI.begin());
    g_noteOns = 0;
    client.setHandleNoteOn(countNoteOn);
    setOutputPin(client, 5, 60);
    stubPinLevels()[5] = LOW;

    client.espnowMIDI.OnDataRecv(kDongle, kNoteOn, 3);
    REQUIRE(g_noteOns == 0);
    REQUIRE(stubPinLevels()[5] == LOW);

    client.loop();
    REQUIRE(g_noteOns == 1);
    REQUIRE(stubPinLevels()[5] == HIGH);
}

TEST_CASE("client setDispatchInLoop(false) runs handlers immediately", "[client][dispatch]")
{
    enomik::Client client;
    client.io.begin(); // as Client::begin() does: loads pins, enables SysEx config
    REQUIRE(client.espnowMIDI.begin());
    g_noteOns = 0;
    client.setHandleNoteOn(countNoteOn);
    client.setDispatchInLoop(false);

    client.espnowMIDI.OnDataRecv(kDongle, kNoteOn, 3);
    REQUIRE(g_noteOns == 1);
    client.loop();
    REQUIRE(g_noteOns == 1);
}

TEST_CASE("handlers set directly on client.espnowMIDI still run", "[client][dispatch]")
{
    // As the audio examples do: _client.espnowMIDI.setHandleNoteOn(...)
    enomik::Client client;
    client.io.begin();
    REQUIRE(client.espnowMIDI.begin());
    g_noteOns = 0;
    client.espnowMIDI.setHandleNoteOn(countNoteOn);

    client.espnowMIDI.OnDataRecv(kDongle, kNoteOn, 3);
    REQUIRE(g_noteOns == 1);
}

TEST_CASE("client decodes pitch bend and song position like the core", "[client][dispatch]")
{
    enomik::Client client;
    client.io.begin(); // as Client::begin() does: loads pins, enables SysEx config
    REQUIRE(client.espnowMIDI.begin());
    static int bend = 0;
    static int songPos = 0;
    client.setHandlePitchBend([](byte, int value) { bend = value; });
    client.setHandleSongPosition([](uint16_t value) { songPos = value; });

    const uint8_t pb[3] = {0xE0, 0x00, 0x00}; // lowest bend
    const uint8_t sp[3] = {0xF2, 0x2C, 0x02}; // 300
    client.espnowMIDI.OnDataRecv(kDongle, pb, 3);
    client.espnowMIDI.OnDataRecv(kDongle, sp, 3);
    client.loop();
    REQUIRE(bend == -8192);
    REQUIRE(songPos == 300);
}

TEST_CASE("client: notes arriving while the pin configuration changes", "[client][dispatch][threads]")
{
    // "wifi" thread = ESP-NOW receive callback; main thread = loop() applying
    // SysEx pin configurations, as when a configurator talks over USB.
    enomik::Client client;
    client.io.begin(); // as Client::begin() does: loads pins, enables SysEx config
    REQUIRE(client.espnowMIDI.begin());
    std::atomic<bool> stop{false};
    std::atomic<bool> receiving{false};
    std::thread wifi([&]() {
        while (!stop)
        {
            client.espnowMIDI.OnDataRecv(kDongle, kNoteOn, 3);
            receiving = true;
        }
    });
    while (!receiving)
    {
        std::this_thread::yield();
    }
    for (int i = 0; i < 2000; ++i)
    {
        setOutputPin(client, static_cast<uint8_t>(i % 8), 60);
        client.loop();
    }
    stop = true;
    wifi.join();

    // The configuration survived intact: one more note switches all 8 pins.
    for (int i = 0; i < MIDI_INBOX_SIZE; ++i)
    {
        client.loop(); // drain what is left
    }
    for (int pin = 0; pin < 8; ++pin)
    {
        stubPinLevels()[pin] = LOW;
    }
    client.espnowMIDI.OnDataRecv(kDongle, kNoteOn, 3);
    client.loop();
    for (int pin = 0; pin < 8; ++pin)
    {
        REQUIRE(stubPinLevels()[pin] == HIGH);
    }
}

namespace {

std::vector<midi_sysex_message> sendIoRequest(enomik::Client &client, enomik::SysExCommand cmd,
                                              const char *payload = "")
{
    std::vector<midi_sysex_message> sent;
    client.io.setOnSysExSendRequest([&sent](midi_sysex_message msg) { sent.push_back(msg); });
    const auto request = enomik::SysExEncoder::encodeName(payload, cmd);
    client.io.onSysEx(request.data, request.length);
    return sent;
}

std::string namePayload(const midi_sysex_message &msg)
{
    return std::string(reinterpret_cast<const char *>(msg.data + 5), msg.length - 6);
}

} // namespace

TEST_CASE("client name: default, sketch name and app override", "[client][name]")
{
    enomik::Client client;
    client.io.begin();
    REQUIRE(std::string(client.getName()) == "enomik");

    client.setName("client_buttons");
    REQUIRE(std::string(client.getName()) == "client_buttons");

    auto sent = sendIoRequest(client, enomik::SysExCommand::GET_NAME);
    REQUIRE(sent.size() == 1);
    REQUIRE(sent[0].data[4] == static_cast<uint8_t>(enomik::SysExCommand::GET_NAME_RESPONSE));
    REQUIRE(namePayload(sent[0]) == "client_buttons");

    sent = sendIoRequest(client, enomik::SysExCommand::SET_NAME, "pad left");
    REQUIRE(sent.size() == 1);
    REQUIRE(sent[0].data[4] == static_cast<uint8_t>(enomik::SysExCommand::SET_NAME_RESPONSE));
    REQUIRE(namePayload(sent[0]) == "pad left");
    REQUIRE(std::string(client.getName()) == "pad left");

    // Sketch default changes do not hide the app override
    client.setName("other");
    REQUIRE(std::string(client.getName()) == "pad left");

    // Empty SET_NAME clears the override
    sent = sendIoRequest(client, enomik::SysExCommand::SET_NAME, "");
    REQUIRE(namePayload(sent[0]) == "other");

    // RESET clears it too
    sendIoRequest(client, enomik::SysExCommand::SET_NAME, "pad left");
    sendIoRequest(client, enomik::SysExCommand::RESET);
    REQUIRE(std::string(client.getName()) == "other");
}

TEST_CASE("client name is part of the GET_CONFIG stream", "[client][name]")
{
    enomik::Client client;
    client.io.begin();
    client.setName("client_test");

    const auto sent = sendIoRequest(client, enomik::SysExCommand::GET_CONFIG);
    REQUIRE(sent.size() >= 2);
    const auto &last = sent[sent.size() - 1];
    const auto &name = sent[sent.size() - 2];
    REQUIRE(last.data[4] == static_cast<uint8_t>(enomik::SysExCommand::GET_CONFIG_RESPONSE));
    REQUIRE(name.data[4] == static_cast<uint8_t>(enomik::SysExCommand::GET_NAME_RESPONSE));
    REQUIRE(namePayload(name) == "client_test");
}
