// ESP-NOW wire format: what the dongle sends and what the core receive path
// accepts. Uses the real enomik::Dongle and esp_now_midi with the host stubs in
// test/native/stubs (esp_now_send() records every packet).
//
// esp_now_midi.h defines a static member in the header, so it may only be
// included by this one test translation unit.

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <cstring>
#include <vector>

#include "arduino_stubs.h"
#include "enomik_dongle.h"

namespace {

const uint8_t kPeerA[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
const uint8_t kPeerB[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
uint8_t kSender[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};

std::vector<uint8_t> bytes(std::initializer_list<int> list)
{
    std::vector<uint8_t> v;
    for (int b : list)
    {
        v.push_back(static_cast<uint8_t>(b));
    }
    return v;
}

// Records what the core receive path dispatches.
struct Received
{
    int noteOns = 0;
    int programChanges = 0;
    int pitchBends = 0;
    int clocks = 0;
    int songPositions = 0;
    int lastChannel = -1;
    int lastA = -1;
    int lastB = -1;
};
Received g_rx;

void attachHandlers(esp_now_midi &rx)
{
    g_rx = Received{};
    rx.setHandleNoteOn([](byte ch, byte note, byte vel) {
        g_rx.noteOns++;
        g_rx.lastChannel = ch;
        g_rx.lastA = note;
        g_rx.lastB = vel;
    });
    rx.setHandleProgramChange([](byte ch, byte program) {
        g_rx.programChanges++;
        g_rx.lastChannel = ch;
        g_rx.lastA = program;
    });
    rx.setHandlePitchBend([](byte ch, int value) {
        g_rx.pitchBends++;
        g_rx.lastChannel = ch;
        g_rx.lastA = value;
    });
    rx.setHandleClock([]() { g_rx.clocks++; });
    rx.setHandleSongPosition([](uint16_t value) {
        g_rx.songPositions++;
        g_rx.lastA = static_cast<int>(value);
    });
}

// Delivers bytes like the radio does: in a heap buffer of exactly that length,
// so an out-of-bounds read is caught by AddressSanitizer builds.
void deliver(esp_now_midi &rx, const std::vector<uint8_t> &data)
{
    uint8_t *buf = static_cast<uint8_t *>(malloc(data.empty() ? 1 : data.size()));
    if (!data.empty())
    {
        memcpy(buf, data.data(), data.size());
    }
    rx.OnDataRecv(kSender, buf, static_cast<int>(data.size()));
    free(buf);
}

} // namespace

// --- Dongle send format ----------------------------------------------------

TEST_CASE("dongle sends the 3-byte MIDI wire format, not its internal struct", "[espnow][wire]")
{
    enomik::Dongle dongle;
    REQUIRE(dongle.espnowMIDI.begin()); // as firmware does (initializes the peer list)
    REQUIRE(dongle.espnowMIDI.addPeer(kPeerA));
    stubEspNowSent().clear();

    dongle.sendNoteOn(60, 100, 1);
    REQUIRE(stubEspNowSent().size() == 1);
    REQUIRE(memcmp(stubEspNowSent()[0].dest, kPeerA, 6) == 0);
    REQUIRE(stubEspNowSent()[0].data == bytes({0x90, 60, 100}));

    stubEspNowSent().clear();
    dongle.sendNoteOff(60, 0, 16);
    REQUIRE(stubEspNowSent()[0].data == bytes({0x8F, 60, 0}));

    stubEspNowSent().clear();
    dongle.sendControlChange(7, 127, 2);
    REQUIRE(stubEspNowSent()[0].data == bytes({0xB1, 7, 127}));
}

TEST_CASE("dongle sends short messages with their real length", "[espnow][wire]")
{
    enomik::Dongle dongle;
    REQUIRE(dongle.espnowMIDI.begin()); // as firmware does (initializes the peer list)
    REQUIRE(dongle.espnowMIDI.addPeer(kPeerA));

    stubEspNowSent().clear();
    dongle.sendProgramChange(5, 3);
    REQUIRE(stubEspNowSent()[0].data == bytes({0xC2, 5}));

    stubEspNowSent().clear();
    dongle.sendAfterTouch(64, 1);
    REQUIRE(stubEspNowSent()[0].data == bytes({0xD0, 64}));

    stubEspNowSent().clear();
    dongle.sendStop();
    REQUIRE(stubEspNowSent()[0].data == bytes({0xFC}));

    stubEspNowSent().clear();
    dongle.sendClock();
    REQUIRE(stubEspNowSent()[0].data == bytes({0xF8}));

    stubEspNowSent().clear();
    dongle.sendSongPosition(300); // 300 = 0x12C -> LSB 0x2C, MSB 0x02
    REQUIRE(stubEspNowSent()[0].data == bytes({0xF2, 0x2C, 0x02}));
}

TEST_CASE("dongle pitch bend bytes match the core encoding", "[espnow][wire]")
{
    enomik::Dongle dongle;
    REQUIRE(dongle.espnowMIDI.begin()); // as firmware does (initializes the peer list)
    REQUIRE(dongle.espnowMIDI.addPeer(kPeerA));

    stubEspNowSent().clear();
    dongle.sendPitchBend(0, 1); // center = 8192 = 0x2000 -> LSB 0x00, MSB 0x40
    REQUIRE(stubEspNowSent()[0].data == bytes({0xE0, 0x00, 0x40}));

    // Same value through the core API must produce identical bytes.
    esp_now_midi core;
    REQUIRE(core.begin());
    core.addPeer(kPeerA);
    stubEspNowSent().clear();
    core.sendPitchBend(-1000, 1);
    const std::vector<uint8_t> coreBytes = stubEspNowSent()[0].data;
    stubEspNowSent().clear();
    dongle.sendPitchBend(-1000, 1);
    REQUIRE(stubEspNowSent()[0].data == coreBytes);
}

TEST_CASE("dongle skips muted peers and sends to the others", "[espnow][wire]")
{
    enomik::Dongle dongle;
    REQUIRE(dongle.espnowMIDI.begin()); // as firmware does (initializes the peer list)
    REQUIRE(dongle.espnowMIDI.addPeer(kPeerA));
    REQUIRE(dongle.espnowMIDI.addPeer(kPeerB));
    REQUIRE(dongle.setMuted(kPeerA, true));

    stubEspNowSent().clear();
    dongle.sendNoteOn(64, 90, 1);
    REQUIRE(stubEspNowSent().size() == 1);
    REQUIRE(memcmp(stubEspNowSent()[0].dest, kPeerB, 6) == 0);
    REQUIRE(stubEspNowSent()[0].data == bytes({0x90, 64, 90}));
}

TEST_CASE("dongle -> receiver round trip reaches the MIDI handlers", "[espnow][wire]")
{
    enomik::Dongle dongle;
    REQUIRE(dongle.espnowMIDI.begin()); // as firmware does (initializes the peer list)
    REQUIRE(dongle.espnowMIDI.addPeer(kPeerA));
    esp_now_midi receiver;
    REQUIRE(receiver.begin());
    attachHandlers(receiver);

    stubEspNowSent().clear();
    dongle.sendNoteOn(61, 77, 10);
    dongle.sendProgramChange(9, 4);
    dongle.sendPitchBend(1234, 2);
    dongle.sendClock();
    dongle.sendSongPosition(1000);
    REQUIRE(stubEspNowSent().size() == 5);
    for (const auto &packet : stubEspNowSent())
    {
        deliver(receiver, packet.data);
    }

    REQUIRE(g_rx.noteOns == 1);
    REQUIRE(g_rx.programChanges == 1);
    REQUIRE(g_rx.pitchBends == 1);
    REQUIRE(g_rx.clocks == 1);
    REQUIRE(g_rx.songPositions == 1);
    REQUIRE(g_rx.lastA == 1000);
}

// --- Core receive path -----------------------------------------------------

TEST_CASE("receive dispatches 1-, 2- and 3-byte packets", "[espnow][receive]")
{
    esp_now_midi rx;
    REQUIRE(rx.begin());
    attachHandlers(rx);

    deliver(rx, bytes({0x92, 60, 100}));
    REQUIRE(g_rx.noteOns == 1);
    REQUIRE(g_rx.lastChannel == 3);
    REQUIRE(g_rx.lastA == 60);
    REQUIRE(g_rx.lastB == 100);

    deliver(rx, bytes({0xC0, 12}));
    REQUIRE(g_rx.programChanges == 1);
    REQUIRE(g_rx.lastA == 12);

    deliver(rx, bytes({0xF8}));
    REQUIRE(g_rx.clocks == 1);
}

TEST_CASE("receive ignores longer packets without reading past them", "[espnow][receive]")
{
    // 4..128 bytes take the (unhandled) SysEx branch. Each buffer is exactly
    // its length: the old code read sizeof(midi_sysex_message) = 129 bytes.
    esp_now_midi rx;
    REQUIRE(rx.begin());
    attachHandlers(rx);

    for (size_t len : {4u, 7u, 64u, 128u, 129u, 200u, 250u})
    {
        std::vector<uint8_t> data(len, 0x00);
        data[0] = 0x90; // looks like a note on, must still not be dispatched
        data[1] = 60;
        data[2] = 100;
        deliver(rx, data);
    }
    REQUIRE(g_rx.noteOns == 0);

    // The old 7-byte dongle format (internal struct) is not misread either.
    midi_message legacy{};
    legacy.channel = 1;
    legacy.status = MIDI_NOTE_ON;
    legacy.firstByte = 60;
    legacy.secondByte = 100;
    std::vector<uint8_t> raw(reinterpret_cast<uint8_t *>(&legacy),
                             reinterpret_cast<uint8_t *>(&legacy) + sizeof(legacy));
    deliver(rx, raw);
    REQUIRE(g_rx.noteOns == 0);
}

TEST_CASE("receive tolerates empty and null packets", "[espnow][receive]")
{
    esp_now_midi rx;
    REQUIRE(rx.begin());
    attachHandlers(rx);
    rx.OnDataRecv(kSender, nullptr, 3);
    rx.OnDataRecv(kSender, nullptr, 0);
    deliver(rx, {});
    uint8_t one = 0x90;
    rx.OnDataRecv(kSender, &one, -1);
    REQUIRE(g_rx.noteOns == 0);
}
