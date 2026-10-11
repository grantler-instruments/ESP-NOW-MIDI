// SysEx ANNOUNCE: clients tell peers who they are at boot, after pairing and as
// an idle heartbeat; the dongle keeps what it hears and tracks who is online.

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <string>
#include <vector>

#include "arduino_stubs.h"
#include "client_host_stubs.h"
#include "enomik_client.h"
#include "enomik_dongle.h"

namespace {

uint8_t kDongleMac[6] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60};
uint8_t kClientMac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
uint8_t kOtherMac[6] = {0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};

// One sent ESP-NOW frame as a complete SysEx message (single-fragment frames only).
bool frameToSysEx(const std::vector<uint8_t> &frame, std::vector<uint8_t> &sysex)
{
    esp_now_midi_sysex::FrameView view;
    if (!esp_now_midi_sysex::parseFrame(frame.data(), static_cast<int>(frame.size()), view) ||
        view.total != 1)
    {
        return false;
    }
    sysex.assign(view.payload, view.payload + view.payloadLen);
    return true;
}

int countAnnounces()
{
    int count = 0;
    for (const auto &p : stubEspNowSent())
    {
        std::vector<uint8_t> sysex;
        if (frameToSysEx(p.data, sysex) && enomik::SysExDecoder::isAnnounce(sysex.data(), sysex.size()))
            count++;
    }
    return count;
}

void deliver(esp_now_midi &rx, const uint8_t mac[6], const std::vector<uint8_t> &data)
{
    rx.OnDataRecv(mac, data.data(), static_cast<int>(data.size()));
}

// What a client with this name sends, as ESP-NOW frames.
std::vector<std::vector<uint8_t>> announceFrames(const char *name)
{
    const auto pkt = enomik::SysExEncoder::encodeAnnounce(enomik::DeviceKind::CLIENT, name);
    uint8_t frame[esp_now_midi_sysex::MAX_FRAME];
    const size_t len = esp_now_midi_sysex::encodeFrame(frame, sizeof(frame), 0, 1, pkt.length, pkt.data, pkt.length);
    return {std::vector<uint8_t>(frame, frame + len)};
}

std::vector<uint8_t> sysexFrame(const std::vector<uint8_t> &sysex)
{
    uint8_t frame[esp_now_midi_sysex::MAX_FRAME];
    const size_t len = esp_now_midi_sysex::encodeFrame(frame, sizeof(frame), 0, 1,
                                                       static_cast<uint16_t>(sysex.size()), sysex.data(),
                                                       static_cast<uint16_t>(sysex.size()));
    return std::vector<uint8_t>(frame, frame + len);
}

} // namespace

// --- Codec ---------------------------------------------------------------------

TEST_CASE("announce encodes kind, version and name and decodes back", "[announce][codec]")
{
    const auto pkt = enomik::SysExEncoder::encodeAnnounce(enomik::DeviceKind::CLIENT, "client_test");
    REQUIRE(pkt.data[4] == static_cast<uint8_t>(enomik::SysExCommand::ANNOUNCE));
    REQUIRE(pkt.length == enomik::SysExPacket::MIN_PACKET_SIZE + enomik::ANNOUNCE_HEADER_SIZE + 11);
    for (uint16_t i = 1; i < pkt.length - 1; i++)
        REQUIRE(pkt.data[i] < 0x80);

    enomik::Announce info;
    REQUIRE(enomik::SysExDecoder::decodeAnnounceMessage(pkt.data, pkt.length, info));
    REQUIRE(info.kind == enomik::DeviceKind::CLIENT);
    REQUIRE(info.major == ESP_NOW_MIDI_VERSION_MAJOR);
    REQUIRE(info.minor == ESP_NOW_MIDI_VERSION_MINOR);
    REQUIRE(info.patch == ESP_NOW_MIDI_VERSION_PATCH);
    REQUIRE(std::string(info.name) == "client_test");
}

TEST_CASE("announce with an empty name is valid", "[announce][codec]")
{
    const auto pkt = enomik::SysExEncoder::encodeAnnounce(enomik::DeviceKind::DONGLE, "");
    enomik::Announce info;
    REQUIRE(enomik::SysExDecoder::decodeAnnounceMessage(pkt.data, pkt.length, info));
    REQUIRE(info.kind == enomik::DeviceKind::DONGLE);
    REQUIRE(info.name[0] == '\0');
}

TEST_CASE("announce decoding rejects broken payloads", "[announce][codec]")
{
    enomik::Announce info;
    const uint8_t shortPayload[] = {0x00, 0, 19};
    REQUIRE_FALSE(enomik::SysExDecoder::decodeAnnounce(shortPayload, sizeof(shortPayload), info));

    const uint8_t badKind[] = {0x05, 0, 19, 0};
    REQUIRE_FALSE(enomik::SysExDecoder::decodeAnnounce(badKind, sizeof(badKind), info));

    const uint8_t badName[] = {0x00, 0, 19, 0, 'a', 0x01};
    REQUIRE_FALSE(enomik::SysExDecoder::decodeAnnounce(badName, sizeof(badName), info));

    std::vector<uint8_t> longName = {0x00, 0, 19, 0};
    longName.insert(longName.end(), enomik::MAX_NAME_LENGTH + 1, 'x');
    REQUIRE_FALSE(enomik::SysExDecoder::decodeAnnounce(longName.data(), static_cast<uint16_t>(longName.size()), info));

    // Wrong major version: not decoded, but still recognized as an announce
    auto pkt = enomik::SysExEncoder::encodeAnnounce(enomik::DeviceKind::CLIENT, "x");
    pkt.data[2] = enomik::PROTOCOL_VERSION_MAJOR + 1;
    REQUIRE_FALSE(enomik::SysExDecoder::decodeAnnounceMessage(pkt.data, pkt.length, info));
    REQUIRE(enomik::SysExDecoder::isAnnounce(pkt.data, pkt.length));
}

TEST_CASE("encoder drops characters a name may not contain", "[announce][codec]")
{
    const auto pkt = enomik::SysExEncoder::encodeAnnounce(enomik::DeviceKind::CLIENT, "a\tb\xC3\xA9");
    enomik::Announce info;
    REQUIRE(enomik::SysExDecoder::decodeAnnounceMessage(pkt.data, pkt.length, info));
    REQUIRE(std::string(info.name) == "ab");
}

// --- Core ------------------------------------------------------------------------

TEST_CASE("SysEx handler with sender gets the MAC", "[announce][core]")
{
    static std::vector<uint8_t> from;
    static std::vector<uint8_t> received;
    from.clear();
    received.clear();

    esp_now_midi rx;
    REQUIRE(rx.begin());
    rx.setHandleSysExFrom([](const uint8_t *mac, uint8_t *data, uint16_t length) {
        from.assign(mac, mac + 6);
        received.assign(data, data + length);
    });

    const std::vector<uint8_t> sysex = {0xF0, 0x43, 0x01, 0xF7};
    deliver(rx, kClientMac, sysexFrame(sysex));
    REQUIRE(from == std::vector<uint8_t>(kClientMac, kClientMac + 6));
    REQUIRE(received == sysex);
}

TEST_CASE("last seen is tracked for peers only", "[announce][core]")
{
    esp_now_midi rx;
    REQUIRE(rx.begin(false, false)); // no auto-discovery
    REQUIRE(rx.addPeer(kClientMac));

    uint32_t seen = 0;
    REQUIRE_FALSE(rx.getPeerLastSeen(kClientMac, seen));

    millisNow() = 5000;
    deliver(rx, kClientMac, {0x90, 60, 100});
    REQUIRE(rx.getPeerLastSeen(kClientMac, seen));
    REQUIRE(seen == 5000);

    deliver(rx, kOtherMac, {0x90, 60, 100});
    REQUIRE_FALSE(rx.getPeerLastSeen(kOtherMac, seen));

    // Foreign traffic does not count as a sign of life
    millisNow() = 6000;
    deliver(rx, kClientMac, {0x12, 0x34});
    REQUIRE(rx.getPeerLastSeen(kClientMac, seen));
    REQUIRE(seen == 5000);
}

// --- Client --------------------------------------------------------------------

TEST_CASE("client announces after a peer is added, not a CC", "[announce][client]")
{
    millisNow() = 1000;
    enomik::Client client;
    client.setName("tester");
    REQUIRE(client.begin());
    stubEspNowSent().clear();

    REQUIRE(client.addPeer(kDongleMac));
    REQUIRE(stubEspNowSent().size() == 1);
    REQUIRE(countAnnounces() == 1);

    std::vector<uint8_t> sysex;
    REQUIRE(frameToSysEx(stubEspNowSent()[0].data, sysex));
    enomik::Announce info;
    REQUIRE(enomik::SysExDecoder::decodeAnnounceMessage(sysex.data(), static_cast<uint16_t>(sysex.size()), info));
    REQUIRE(std::string(info.name) == "tester");
    REQUIRE(info.kind == enomik::DeviceKind::CLIENT);
}

TEST_CASE("client announce goes once to each peer", "[announce][client]")
{
    enomik::Client client;
    REQUIRE(client.begin());
    REQUIRE(client.addPeer(kDongleMac));
    REQUIRE(client.addPeer(kOtherMac));
    stubEspNowSent().clear();

    REQUIRE(client.sendAnnounce());
    REQUIRE(stubEspNowSent().size() == 2);
    REQUIRE(countAnnounces() == 2);
}

TEST_CASE("client sends a heartbeat only after being idle", "[announce][client]")
{
    millisNow() = 10000;
    enomik::Client client;
    REQUIRE(client.begin());
    REQUIRE(client.addPeer(kDongleMac)); // announces at 10 s
    client.setHeartbeatInterval(60000);
    stubEspNowSent().clear();

    millisNow() = 10000 + 59999;
    client.loop();
    REQUIRE(stubEspNowSent().empty());

    millisNow() = 10000 + 60000;
    client.loop();
    REQUIRE(countAnnounces() == 1);

    // Other traffic counts as a sign of life and resets the timer
    stubEspNowSent().clear();
    millisNow() += 30000;
    client.sendNoteOn(60, 100, 1);
    millisNow() += 40000;
    client.loop();
    REQUIRE(countAnnounces() == 0);
    millisNow() += 20000;
    client.loop();
    REQUIRE(countAnnounces() == 1);
}

TEST_CASE("client retries a failing heartbeat once per interval", "[announce][client]")
{
    millisNow() = 0;
    enomik::Client client;
    REQUIRE(client.begin());
    REQUIRE(client.addPeer(kDongleMac)); // announce accepted at 0 s
    client.setHeartbeatInterval(60000);

    // ESP-NOW refuses every send from now on (the stub knows no peers)
    stubEspNow().peers.clear();
    stubEspNowSent().clear();

    millisNow() = 60000;
    client.loop();
    client.loop();
    client.loop();
    millisNow() = 60500;
    client.loop();
    const auto failedAfterFirst = client.espnowMIDI.getSendStats().failed;

    millisNow() = 119999;
    client.loop();
    REQUIRE(client.espnowMIDI.getSendStats().failed == failedAfterFirst);
    millisNow() = 120000;
    client.loop();
    REQUIRE(client.espnowMIDI.getSendStats().failed == failedAfterFirst + 1);
}

TEST_CASE("client heartbeat can be turned off", "[announce][client]")
{
    millisNow() = 0;
    enomik::Client client;
    REQUIRE(client.begin());
    REQUIRE(client.addPeer(kDongleMac));
    client.setHeartbeatInterval(0);
    stubEspNowSent().clear();

    millisNow() = 10 * 60 * 1000;
    client.loop();
    REQUIRE(stubEspNowSent().empty());
}

TEST_CASE("client without peers sends no heartbeat", "[announce][client]")
{
    millisNow() = 0;
    enomik::Client client;
    REQUIRE(client.begin());
    stubEspNowSent().clear();

    millisNow() = 10 * 60 * 1000;
    client.loop();
    REQUIRE(stubEspNowSent().empty());
}

TEST_CASE("client does not pass announces to the sketch", "[announce][client]")
{
    enomik::Client client;
    REQUIRE(client.begin());
    int calls = 0;
    client.setHandleSysEx([&](uint8_t *, unsigned int) { calls++; });

    for (const auto &frame : announceFrames("other"))
        deliver(client.espnowMIDI, kOtherMac, frame);
    deliver(client.espnowMIDI, kOtherMac, sysexFrame({0xF0, 0x43, 0x01, 0xF7}));
    client.loop();
    REQUIRE(calls == 1);
}

// --- Dongle --------------------------------------------------------------------

TEST_CASE("dongle learns a client's name from its announce", "[announce][dongle]")
{
    millisNow() = 1000;
    enomik::Dongle dongle;
    REQUIRE(dongle.espnowMIDI.begin());

    char name[enomik::MAX_NAME_LENGTH + 1];
    REQUIRE_FALSE(dongle.getPeerName(kClientMac, name));

    for (const auto &frame : announceFrames("client_test"))
        deliver(dongle.espnowMIDI, kClientMac, frame);
    dongle.processReceived();

    REQUIRE(dongle.espnowMIDI.hasPeer(kClientMac)); // auto-discovered from the announce
    REQUIRE(dongle.getPeerName(kClientMac, name));
    REQUIRE(std::string(name) == "client_test");
    enomik::Announce info;
    REQUIRE(dongle.getPeerAnnounce(kClientMac, info));
    REQUIRE(info.kind == enomik::DeviceKind::CLIENT);
    REQUIRE(info.minor == ESP_NOW_MIDI_VERSION_MINOR);
    REQUIRE_FALSE(dongle.usbSysExPending()); // never sent to the host
}

TEST_CASE("dongle forwards other SysEx to the host", "[announce][dongle]")
{
    enomik::Dongle dongle;
    REQUIRE(dongle.espnowMIDI.begin());
    deliver(dongle.espnowMIDI, kClientMac, sysexFrame({0xF0, 0x43, 0x01, 0xF7}));
    REQUIRE(dongle.usbSysExPending());
}

TEST_CASE("dongle peer status follows heartbeats and timeout", "[announce][dongle]")
{
    millisNow() = 1000;
    enomik::Dongle dongle;
    REQUIRE(dongle.espnowMIDI.begin());
    dongle.setPeerTimeout(180000);

    REQUIRE(dongle.getPeerStatus(kClientMac) == enomik::PeerStatus::Unknown);

    for (const auto &frame : announceFrames("a"))
        deliver(dongle.espnowMIDI, kClientMac, frame);
    REQUIRE(dongle.getPeerStatus(kClientMac) == enomik::PeerStatus::Online);

    millisNow() = 1000 + 179999;
    REQUIRE(dongle.getPeerStatus(kClientMac) == enomik::PeerStatus::Online);
    millisNow() = 1000 + 180000;
    REQUIRE(dongle.getPeerStatus(kClientMac) == enomik::PeerStatus::Offline);

    // Any MIDI brings it back
    deliver(dongle.espnowMIDI, kClientMac, {0x90, 60, 100});
    REQUIRE(dongle.getPeerStatus(kClientMac) == enomik::PeerStatus::Online);
}

TEST_CASE("dongle keeps peers that never announce as unknown when silent", "[announce][dongle]")
{
    millisNow() = 1000;
    enomik::Dongle dongle;
    REQUIRE(dongle.espnowMIDI.begin());

    deliver(dongle.espnowMIDI, kOtherMac, {0x90, 60, 100}); // older client: plain MIDI only
    REQUIRE(dongle.getPeerStatus(kOtherMac) == enomik::PeerStatus::Online);
    millisNow() += dongle.getPeerTimeout();
    REQUIRE(dongle.getPeerStatus(kOtherMac) == enomik::PeerStatus::Unknown);
}

TEST_CASE("dongle forgets what a removed peer announced", "[announce][dongle]")
{
    enomik::Dongle dongle;
    REQUIRE(dongle.espnowMIDI.begin());
    for (const auto &frame : announceFrames("gone"))
        deliver(dongle.espnowMIDI, kClientMac, frame);

    char name[enomik::MAX_NAME_LENGTH + 1];
    REQUIRE(dongle.getPeerName(kClientMac, name));
    REQUIRE(dongle.removePeer(kClientMac));
    REQUIRE_FALSE(dongle.getPeerName(kClientMac, name));
    REQUIRE(dongle.getPeerStatus(kClientMac) == enomik::PeerStatus::Unknown);
}

TEST_CASE("dongle updates a name that changed", "[announce][dongle]")
{
    enomik::Dongle dongle;
    REQUIRE(dongle.espnowMIDI.begin());
    for (const auto &frame : announceFrames("before"))
        deliver(dongle.espnowMIDI, kClientMac, frame);
    for (const auto &frame : announceFrames("after"))
        deliver(dongle.espnowMIDI, kClientMac, frame);

    char name[enomik::MAX_NAME_LENGTH + 1];
    REQUIRE(dongle.getPeerName(kClientMac, name));
    REQUIRE(std::string(name) == "after");
}
