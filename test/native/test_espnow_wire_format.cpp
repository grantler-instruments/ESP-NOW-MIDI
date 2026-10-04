// ESP-NOW wire format: what the dongle sends and what the core receive path
// accepts. Uses the real enomik::Dongle and esp_now_midi with the host stubs in
// test/native/stubs (esp_now_send() records every packet).
//
// esp_now_midi.h defines a static member in the header, so it may only be
// included by this one test translation unit.

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <thread>
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

// --- Peer list -------------------------------------------------------------

namespace {

void macFor(uint8_t out[6], int n)
{
    const uint8_t base[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x00};
    memcpy(out, base, 6);
    out[4] = static_cast<uint8_t>(n >> 8);
    out[5] = static_cast<uint8_t>(n);
}

} // namespace

TEST_CASE("peer list: add, duplicate, remove keeps order", "[espnow][peers]")
{
    esp_now_midi midi;
    REQUIRE(midi.begin());
    uint8_t a[6], b[6], c[6];
    macFor(a, 1);
    macFor(b, 2);
    macFor(c, 3);

    REQUIRE(midi.addPeer(a));
    REQUIRE(midi.addPeer(b));
    REQUIRE(midi.addPeer(c));
    REQUIRE_FALSE(midi.addPeer(b));
    REQUIRE(midi.getPeersCount() == 3);

    REQUIRE(midi.removePeer(b));
    REQUIRE_FALSE(midi.removePeer(b));
    uint8_t out[6];
    REQUIRE(midi.getPeer(0, out));
    REQUIRE(memcmp(out, a, 6) == 0);
    REQUIRE(midi.getPeer(1, out));
    REQUIRE(memcmp(out, c, 6) == 0);
    REQUIRE_FALSE(midi.getPeer(2, out));
    REQUIRE(stubEspNowPeerCount() == 2);

    REQUIRE(midi.removePeer(0));
    REQUIRE(midi.getPeersCount() == 1);
    midi.clearPeers();
    REQUIRE(midi.getPeersCount() == 0);
    REQUIRE(stubEspNowPeerCount() == 0);
}

TEST_CASE("peer list: full list rejects more and leaks no registration", "[espnow][peers]")
{
    esp_now_midi midi;
    REQUIRE(midi.begin());
    uint8_t mac[6];
    for (int i = 0; i < MAX_PEERS; ++i)
    {
        macFor(mac, i);
        REQUIRE(midi.addPeer(mac));
    }
    macFor(mac, 999);
    REQUIRE_FALSE(midi.addPeer(mac));
    REQUIRE(midi.getPeersCount() == MAX_PEERS);
    REQUIRE(stubEspNowPeerCount() == MAX_PEERS);

    uint8_t macs[MAX_PEERS][6];
    REQUIRE(midi.copyPeers(macs, 5) == 5);
    REQUIRE(midi.copyPeers(macs, -1) == 0);
    REQUIRE(midi.copyPeers(macs, MAX_PEERS) == MAX_PEERS);
}

TEST_CASE("peer list: auto-discovery adds a sender exactly once", "[espnow][peers]")
{
    esp_now_midi midi;
    REQUIRE(midi.begin()); // auto discovery on by default
    uint8_t sender[6];
    macFor(sender, 42);
    const uint8_t clock = 0xF8;
    midi.OnDataRecv(sender, &clock, 1);
    midi.OnDataRecv(sender, &clock, 1);
    REQUIRE(midi.getPeersCount() == 1);
    REQUIRE(midi.hasPeer(sender));
}

TEST_CASE("peer list: concurrent use from the WiFi task and the loop task", "[espnow][peers][threads]")
{
    // Thread "wifi" acts like the receive callback: auto-discovery plus a
    // handler that echoes to all peers. The main thread acts like loop():
    // adds, removes, clears and sends. Run under ThreadSanitizer to catch races.
    esp_now_midi midi;
    REQUIRE(midi.begin());
    stubEspNow().record = false;
    const uint8_t note[3] = {0x90, 60, 100};

    std::thread wifi([&]() {
        uint8_t sender[6];
        for (int i = 0; i < 4000; ++i)
        {
            macFor(sender, i % 40);
            midi.OnDataRecv(sender, note, 3);
            midi.sendToAllPeers(note, 3);
        }
    });

    uint8_t mac[6];
    for (int i = 0; i < 4000; ++i)
    {
        macFor(mac, 100 + i % 10);
        midi.addPeer(mac);
        midi.sendToAllPeers(note, 3);
        macFor(mac, (i * 7) % 40);
        midi.removePeer(mac);
        if (i % 500 == 0)
        {
            midi.clearPeers();
        }
    }
    wifi.join();
    stubEspNow().record = true;

    // The list stays consistent: within bounds, no duplicates, and exactly
    // the peers the ESP-NOW driver has registered.
    uint8_t macs[MAX_PEERS][6];
    const int count = midi.copyPeers(macs, MAX_PEERS);
    REQUIRE(count == midi.getPeersCount());
    REQUIRE(static_cast<size_t>(count) == stubEspNowPeerCount());
    for (int i = 0; i < count; ++i)
    {
        for (int j = i + 1; j < count; ++j)
        {
            REQUIRE(memcmp(macs[i], macs[j], 6) != 0);
        }
    }
}

// --- Receive dispatch ------------------------------------------------------

namespace {

int g_messageCalls = 0;
int g_noteOnCalls = 0;
midi_message g_lastMessage{};
uint8_t g_lastMac[6] = {0};

} // namespace

TEST_CASE("setHandleMessage receives every message with its sender", "[espnow][receive]")
{
    esp_now_midi rx;
    REQUIRE(rx.begin());
    g_messageCalls = 0;
    g_noteOnCalls = 0;
    rx.setHandleNoteOn([](byte, byte, byte) { g_noteOnCalls++; });
    rx.setHandleMessage([](const uint8_t *mac, const midi_message &msg) {
        g_messageCalls++;
        g_lastMessage = msg;
        memcpy(g_lastMac, mac, 6);
    });

    deliver(rx, bytes({0x91, 64, 90}));
    REQUIRE(g_messageCalls == 1);
    REQUIRE(g_noteOnCalls == 1); // per-type handlers still run
    REQUIRE(static_cast<int>(g_lastMessage.status) == MIDI_NOTE_ON);
    REQUIRE(g_lastMessage.channel == 2);
    REQUIRE(g_lastMessage.firstByte == 64);
    REQUIRE(memcmp(g_lastMac, kSender, 6) == 0);

    rx.setHandleMessage(nullptr);
    deliver(rx, bytes({0x91, 64, 90}));
    REQUIRE(g_messageCalls == 1);
    REQUIRE(g_noteOnCalls == 2);
}

namespace {

std::atomic<int> g_toHostCalls{0};

} // namespace

TEST_CASE("dongle handles received MIDI in loop, not in the receive callback", "[espnow][dongle]")
{
    enomik::Dongle dongle;
    REQUIRE(dongle.espnowMIDI.begin());
    g_toHostCalls = 0;
    dongle.setToHostFilter([](midi_message &) {
        g_toHostCalls++;
        return true;
    });

    deliver(dongle.espnowMIDI, bytes({0x90, 60, 100}));
    REQUIRE(g_toHostCalls == 0); // only queued in the callback
    dongle.processReceived();
    REQUIRE(g_toHostCalls == 1);

    // Messages the dongle never bridged to USB stay ignored.
    deliver(dongle.espnowMIDI, bytes({0xFE})); // active sensing
    dongle.processReceived();
    REQUIRE(g_toHostCalls == 1);
}

TEST_CASE("dongle drops messages from muted senders", "[espnow][dongle]")
{
    enomik::Dongle dongle;
    REQUIRE(dongle.espnowMIDI.begin());
    g_toHostCalls = 0;
    dongle.setToHostFilter([](midi_message &) {
        g_toHostCalls++;
        return true;
    });

    deliver(dongle.espnowMIDI, bytes({0xF8})); // auto-discovers kSender
    dongle.processReceived();
    REQUIRE(g_toHostCalls == 1);

    REQUIRE(dongle.setMuted(kSender, true));
    deliver(dongle.espnowMIDI, bytes({0x90, 60, 100}));
    dongle.processReceived();
    REQUIRE(g_toHostCalls == 1);

    REQUIRE(dongle.setMuted(kSender, false));
    deliver(dongle.espnowMIDI, bytes({0x90, 60, 100}));
    dongle.processReceived();
    REQUIRE(g_toHostCalls == 2);
}

TEST_CASE("dongle receive runs concurrently with loop without races", "[espnow][dongle][threads]")
{
    // "wifi" thread = ESP-NOW receive callback; main thread = loop() that
    // processes messages and toggles mutes like the menu does.
    enomik::Dongle dongle;
    REQUIRE(dongle.espnowMIDI.begin());
    g_toHostCalls = 0;
    dongle.setToHostFilter([](midi_message &) {
        g_toHostCalls++;
        return true;
    });
    deliver(dongle.espnowMIDI, bytes({0xF8})); // register kSender as peer
    dongle.processReceived();

    std::atomic<bool> stop{false};
    std::atomic<bool> receiving{false};
    std::thread wifi([&]() {
        const uint8_t pkt[3] = {0x90, 60, 100};
        while (!stop)
        {
            dongle.espnowMIDI.OnDataRecv(kSender, pkt, 3);
            receiving = true;
        }
    });
    while (!receiving)
    {
        std::this_thread::yield();
    }
    for (int i = 0; i < 200000; ++i)
    {
        dongle.setMuted(kSender, (i & 1) != 0);
        dongle.processReceived();
    }
    stop = true;
    wifi.join();
    dongle.processReceived();
    REQUIRE(g_toHostCalls > 1);
}

// --- Auto-discovery --------------------------------------------------------

TEST_CASE("isMidiPacket accepts what the library sends", "[espnow][discovery]")
{
    REQUIRE(esp_now_midi::isMidiPacket(bytes({0x90, 60, 100}).data(), 3));
    REQUIRE(esp_now_midi::isMidiPacket(bytes({0xC3, 5}).data(), 2));
    REQUIRE(esp_now_midi::isMidiPacket(bytes({0xF8}).data(), 1));
    REQUIRE(esp_now_midi::isMidiPacket(bytes({0xF2, 0x2C, 0x02}).data(), 3));

    std::vector<uint8_t> sysex(sizeof(midi_sysex_message), 0);
    sysex[0] = 0xF0;
    sysex.back() = 10;
    REQUIRE(esp_now_midi::isMidiPacket(sysex.data(), static_cast<int>(sysex.size())));

    // Everything a real sender would produce: dongle, core send API.
    enomik::Dongle dongle;
    REQUIRE(dongle.espnowMIDI.begin());
    REQUIRE(dongle.espnowMIDI.addPeer(kPeerA));
    stubEspNowSent().clear();
    dongle.sendNoteOn(60, 100, 1);
    dongle.sendProgramChange(3, 2);
    dongle.sendPitchBend(-2000, 3);
    dongle.sendClock();
    dongle.sendSongSelect(4);
    for (const auto &p : stubEspNowSent())
    {
        REQUIRE(esp_now_midi::isMidiPacket(p.data.data(), static_cast<int>(p.data.size())));
    }
}

TEST_CASE("isMidiPacket rejects foreign or broken payloads", "[espnow][discovery]")
{
    REQUIRE_FALSE(esp_now_midi::isMidiPacket(nullptr, 3));
    REQUIRE_FALSE(esp_now_midi::isMidiPacket(bytes({0x90}).data(), 0));
    REQUIRE_FALSE(esp_now_midi::isMidiPacket(bytes({0x40, 60, 100}).data(), 3)); // no status byte
    REQUIRE_FALSE(esp_now_midi::isMidiPacket(bytes({0x90, 60}).data(), 2));      // too short for note on
    REQUIRE_FALSE(esp_now_midi::isMidiPacket(bytes({0xC0, 5, 0}).data(), 3));    // too long for program change
    REQUIRE_FALSE(esp_now_midi::isMidiPacket(bytes({0x90, 60, 0x80}).data(), 3)); // data byte with bit 7
    REQUIRE_FALSE(esp_now_midi::isMidiPacket(bytes({0xF4, 1, 2}).data(), 3));    // undefined status
    REQUIRE_FALSE(esp_now_midi::isMidiPacket(bytes({0xF0, 1, 2}).data(), 3));    // bare sysex start

    std::vector<uint8_t> junk(7, 0x55);
    REQUIRE_FALSE(esp_now_midi::isMidiPacket(junk.data(), 7));
    std::vector<uint8_t> sysex(sizeof(midi_sysex_message), 0);
    REQUIRE_FALSE(esp_now_midi::isMidiPacket(sysex.data(), static_cast<int>(sysex.size()))); // length 0
    std::vector<uint8_t> big(250, 0x90);
    REQUIRE_FALSE(esp_now_midi::isMidiPacket(big.data(), 250));
}

TEST_CASE("auto-discovery ignores senders of foreign traffic", "[espnow][discovery]")
{
    esp_now_midi rx;
    REQUIRE(rx.begin());
    uint8_t mac[6];
    std::vector<uint8_t> junk(24, 0xAB);
    for (int i = 0; i < 40; ++i)
    {
        macFor(mac, 500 + i);
        rx.OnDataRecv(mac, junk.data(), static_cast<int>(junk.size()));
        rx.OnDataRecv(mac, bytes({0x12, 0x34}).data(), 2);
    }
    REQUIRE(rx.getPeersCount() == 0);

    macFor(mac, 1);
    rx.OnDataRecv(mac, bytes({0x90, 60, 100}).data(), 3);
    REQUIRE(rx.getPeersCount() == 1);
    REQUIRE(rx.hasPeer(mac));
}
