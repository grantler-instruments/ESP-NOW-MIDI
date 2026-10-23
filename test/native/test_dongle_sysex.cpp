// enomik::Dongle answers Enomik config SysEx (F0 7D ...) from the USB host:
// GET_MAC, ADD_PEER, GET_PEER, GET_ALL_PEERS, RESET, GET_VERSION; everything else gets
// NOT_READY. Built with HAS_USB_MIDI in its own executable (native_dongle_usb_tests).

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <vector>

#include "arduino_stubs.h"
#include "client_host_stubs.h"
#include "dongle_usb_stubs.h"
#include "enomik_dongle.h"

// The Dongle's USB watchdog looks these TinyUSB symbols up weakly; no endpoint here.
extern "C" bool usbd_edpt_busy(uint8_t, uint8_t) { return false; }
extern "C" uint8_t const *tud_descriptor_configuration_cb(uint8_t) { return nullptr; }

using enomik::SysExCommand;
using enomik::SysExErrorCode;
using Bytes = std::vector<uint8_t>;

namespace {

const uint8_t kDongleMac[6] = {0x84, 0xF7, 0x03, 0xF2, 0x54, 0x62};
const uint8_t kPeerA[6] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60};
const uint8_t kPeerB[6] = {0x11, 0x21, 0x31, 0x41, 0x51, 0x61};

// Started dongle with USB mounted and the USB MIDI handlers registered.
void startDongle(enomik::Dongle &dongle)
{
    memcpy(stubWifiMac(), kDongleMac, 6);
    TinyUSBDevice = TinyUSBDeviceStub();
    DONGLE_USBMIDI = DongleMidiStub();
    REQUIRE(dongle.begin());
    dongle.loop();
    REQUIRE(DONGLE_USBMIDI.sysExHandler != nullptr);
    g_dongle_usb_midi.packets.clear();
    stubEspNowSent().clear();
}

// SysEx from the USB host, as the MIDI library hands it over (F0 ... F7).
void fromHost(Bytes sysex)
{
    DONGLE_USBMIDI.sysExHandler(sysex.data(), static_cast<unsigned>(sysex.size()));
}

void fromHost(const enomik::SysExPacket &pkt)
{
    fromHost(Bytes(pkt.data, pkt.data + pkt.length));
}

enomik::SysExPacket request(SysExCommand cmd)
{
    return enomik::SysExEncoder::encodeSimpleResponse(cmd);
}

enomik::SysExPacket addPeerRequest(const uint8_t mac[6])
{
    auto pkt = enomik::SysExEncoder::encodeMAC(mac);
    pkt.data[4] = static_cast<uint8_t>(SysExCommand::ADD_PEER);
    return pkt;
}

enomik::SysExPacket getPeerRequest(uint8_t index)
{
    return enomik::SysExEncoder::encodeByteResponse(SysExCommand::GET_PEER, index);
}

// SysEx messages the dongle wrote to the USB host, rebuilt from USB-MIDI packets.
std::vector<Bytes> toHost()
{
    std::vector<Bytes> messages;
    Bytes current;
    for (const auto &p : g_dongle_usb_midi.packets)
    {
        const uint8_t cin = p[0] & 0x0F;
        const int count = cin == 0x4 || cin == 0x7 ? 3 : cin == 0x6 ? 2 : cin == 0x5 ? 1 : 0;
        current.insert(current.end(), p.begin() + 1, p.begin() + 1 + count);
        if (cin != 0x4)
        {
            messages.push_back(current);
            current.clear();
        }
    }
    return messages;
}

uint8_t command(const Bytes &msg) { return msg.size() > 4 ? msg[4] : 0; }

Bytes errorFor(SysExCommand request, SysExErrorCode code)
{
    const auto pkt = enomik::SysExEncoder::encodeError(static_cast<uint8_t>(request), code);
    return Bytes(pkt.data, pkt.data + pkt.length);
}

Bytes peerEntry(uint8_t index, const uint8_t mac[6], SysExCommand cmd)
{
    const auto pkt = enomik::SysExEncoder::encodePeerEntry(index, mac, cmd);
    return Bytes(pkt.data, pkt.data + pkt.length);
}

} // namespace

TEST_CASE("dongle answers GET_MAC with its STA MAC", "[dongle][sysex]")
{
    enomik::Dongle dongle;
    startDongle(dongle);

    fromHost(request(SysExCommand::GET_MAC));

    const auto pkt = enomik::SysExEncoder::encodeMAC(kDongleMac);
    const auto replies = toHost();
    REQUIRE(replies.size() == 1);
    REQUIRE(replies[0] == Bytes(pkt.data, pkt.data + pkt.length));
    REQUIRE(command(replies[0]) == static_cast<uint8_t>(SysExCommand::GET_MAC_RESPONSE));
}

TEST_CASE("dongle answers GET_VERSION", "[dongle][sysex]")
{
    enomik::Dongle dongle;
    startDongle(dongle);

    fromHost(request(SysExCommand::GET_VERSION));

    const auto pkt = enomik::SysExEncoder::encodeVersion();
    const auto replies = toHost();
    REQUIRE(replies.size() == 1);
    REQUIRE(replies[0] == Bytes(pkt.data, pkt.data + pkt.length));
}

TEST_CASE("dongle ADD_PEER adds a live and stored peer", "[dongle][sysex]")
{
    enomik::Dongle dongle;
    startDongle(dongle);

    fromHost(addPeerRequest(kPeerA));

    const auto ok = enomik::SysExEncoder::encodeByteResponse(SysExCommand::ADD_PEER_RESPONSE, 1);
    const auto replies = toHost();
    REQUIRE(replies.size() == 1);
    REQUIRE(replies[0] == Bytes(ok.data, ok.data + ok.length));
    REQUIRE(dongle.espnowMIDI.hasPeer(kPeerA));
    REQUIRE(dongle.getPeersCount() == 1);
}

TEST_CASE("dongle ADD_PEER of a known peer reports PEER_ALREADY_EXISTS", "[dongle][sysex]")
{
    enomik::Dongle dongle;
    startDongle(dongle);
    REQUIRE(dongle.addPeer(kPeerA));

    fromHost(addPeerRequest(kPeerA));

    const auto replies = toHost();
    REQUIRE(replies.size() == 1);
    REQUIRE(replies[0] == errorFor(SysExCommand::ADD_PEER, SysExErrorCode::PEER_ALREADY_EXISTS));
    REQUIRE(dongle.getPeersCount() == 1);
}

TEST_CASE("dongle ADD_PEER with a broken MAC reports DECODE_FAILED", "[dongle][sysex]")
{
    enomik::Dongle dongle;
    startDongle(dongle);

    // 5 of the 12 MAC nibbles
    fromHost({0xF0, 0x7D, enomik::PROTOCOL_VERSION_MAJOR, enomik::PROTOCOL_VERSION_MINOR,
              static_cast<uint8_t>(SysExCommand::ADD_PEER), 1, 0, 2, 0, 3, 0xF7});

    const auto replies = toHost();
    REQUIRE(replies.size() == 1);
    REQUIRE(replies[0] == errorFor(SysExCommand::ADD_PEER, SysExErrorCode::DECODE_FAILED));
    REQUIRE(dongle.getPeersCount() == 0);
}

TEST_CASE("dongle GET_PEER returns the peer at an index", "[dongle][sysex]")
{
    enomik::Dongle dongle;
    startDongle(dongle);
    REQUIRE(dongle.addPeer(kPeerA));
    REQUIRE(dongle.addPeer(kPeerB));

    fromHost(getPeerRequest(1));

    const auto replies = toHost();
    REQUIRE(replies.size() == 1);
    REQUIRE(replies[0] == peerEntry(1, kPeerB, SysExCommand::GET_PEER_RESPONSE));
}

TEST_CASE("dongle GET_PEER past the end reports PEER_NOT_FOUND", "[dongle][sysex]")
{
    enomik::Dongle dongle;
    startDongle(dongle);
    REQUIRE(dongle.addPeer(kPeerA));

    fromHost(getPeerRequest(1));

    const auto pkt = enomik::SysExEncoder::encodeError(
        static_cast<uint8_t>(SysExCommand::GET_PEER), SysExErrorCode::PEER_NOT_FOUND, 1);
    const auto replies = toHost();
    REQUIRE(replies.size() == 1);
    REQUIRE(replies[0] == Bytes(pkt.data, pkt.data + pkt.length));
}

TEST_CASE("dongle GET_ALL_PEERS streams every peer, then the end marker", "[dongle][sysex]")
{
    enomik::Dongle dongle;
    startDongle(dongle);
    REQUIRE(dongle.addPeer(kPeerA));
    REQUIRE(dongle.addPeer(kPeerB));

    fromHost(request(SysExCommand::GET_ALL_PEERS));

    const auto end = enomik::SysExEncoder::encodeSimpleResponse(SysExCommand::GET_ALL_PEERS_RESPONSE);
    const auto replies = toHost();
    REQUIRE(replies.size() == 3);
    REQUIRE(replies[0] == peerEntry(0, kPeerA, SysExCommand::GET_ALL_PEERS_RESPONSE));
    REQUIRE(replies[1] == peerEntry(1, kPeerB, SysExCommand::GET_ALL_PEERS_RESPONSE));
    REQUIRE(replies[2] == Bytes(end.data, end.data + end.length));
}

TEST_CASE("dongle GET_ALL_PEERS sends more replies than the USB SysEx queue holds", "[dongle][sysex]")
{
    enomik::Dongle dongle;
    startDongle(dongle);
    for (uint8_t i = 0; i < 5; i++)
    {
        const uint8_t mac[6] = {0x20, 0x00, 0x00, 0x00, 0x00, i};
        REQUIRE(dongle.addPeer(mac));
    }

    fromHost(request(SysExCommand::GET_ALL_PEERS));

    REQUIRE(toHost().size() == 6);
}

TEST_CASE("dongle GET_ALL_PEERS with no peers sends only the end marker", "[dongle][sysex]")
{
    enomik::Dongle dongle;
    startDongle(dongle);

    fromHost(request(SysExCommand::GET_ALL_PEERS));

    const auto replies = toHost();
    REQUIRE(replies.size() == 1);
    REQUIRE(command(replies[0]) == static_cast<uint8_t>(SysExCommand::GET_ALL_PEERS_RESPONSE));
    REQUIRE(replies[0].size() == enomik::SysExPacket::MIN_PACKET_SIZE);
}

TEST_CASE("dongle RESET removes every peer, mute and announce", "[dongle][sysex]")
{
    enomik::Dongle dongle;
    startDongle(dongle);
    REQUIRE(dongle.addPeer(kPeerA));
    REQUIRE(dongle.addPeer(kPeerB));
    REQUIRE(dongle.setMuted(kPeerA, true));

    fromHost(request(SysExCommand::RESET));

    const auto done = enomik::SysExEncoder::encodeSimpleResponse(SysExCommand::RESET_RESPONSE);
    const auto replies = toHost();
    REQUIRE(replies.size() == 1);
    REQUIRE(replies[0] == Bytes(done.data, done.data + done.length));
    REQUIRE(dongle.getPeersCount() == 0);
    REQUIRE_FALSE(dongle.espnowMIDI.hasPeer(kPeerA));
    REQUIRE_FALSE(dongle.espnowMIDI.hasPeer(kPeerB));
    REQUIRE_FALSE(dongle.isMuted(kPeerA));
    REQUIRE(dongle.getPeerStatus(kPeerA) == enomik::PeerStatus::Unknown);
}

TEST_CASE("dongle GET_ALL_PEERS after RESET sends only the end marker", "[dongle][sysex]")
{
    enomik::Dongle dongle;
    startDongle(dongle);
    REQUIRE(dongle.addPeer(kPeerA));

    fromHost(request(SysExCommand::RESET));
    g_dongle_usb_midi.packets.clear();
    fromHost(request(SysExCommand::GET_ALL_PEERS));

    const auto replies = toHost();
    REQUIRE(replies.size() == 1);
    REQUIRE(replies[0].size() == enomik::SysExPacket::MIN_PACKET_SIZE);
}

TEST_CASE("dongle RESET turns power save off", "[dongle][sysex]")
{
    enomik::Dongle dongle;
    startDongle(dongle);
    dongle.setPowerSave(true);
    REQUIRE(dongle.isPowerSave());

    fromHost(request(SysExCommand::RESET));

    REQUIRE_FALSE(dongle.isPowerSave());
}

TEST_CASE("dongle answers client-only commands with NOT_READY", "[dongle][sysex]")
{
    enomik::Dongle dongle;
    startDongle(dongle);

    for (auto cmd : {SysExCommand::GET_ALL_PIN_CONFIGS, SysExCommand::GET_NAME, SysExCommand::SET_POWER_SAVE})
    {
        g_dongle_usb_midi.packets.clear();
        fromHost(request(cmd));
        const auto replies = toHost();
        REQUIRE(replies.size() == 1);
        REQUIRE(replies[0] == errorFor(cmd, SysExErrorCode::NOT_READY));
    }
}

TEST_CASE("dongle never forwards config SysEx to its peers", "[dongle][sysex]")
{
    enomik::Dongle dongle;
    startDongle(dongle);
    REQUIRE(dongle.addPeer(kPeerA));
    stubEspNowSent().clear();

    fromHost(request(SysExCommand::GET_MAC));
    fromHost(request(SysExCommand::GET_VERSION));
    dongle.espnowMIDI.flush(100);

    REQUIRE(stubEspNowSent().empty());
}

TEST_CASE("dongle still forwards other SysEx to its peers without answering", "[dongle][sysex]")
{
    enomik::Dongle dongle;
    startDongle(dongle);
    REQUIRE(dongle.addPeer(kPeerA));
    stubEspNowSent().clear();

    fromHost({0xF0, 0x43, 0x01, 0x02, 0xF7});
    dongle.espnowMIDI.flush(100);

    REQUIRE(stubEspNowSent().size() == 1);
    REQUIRE(g_dongle_usb_midi.packets.empty());
}

TEST_CASE("dongle sends no config reply while USB is not mounted", "[dongle][sysex]")
{
    enomik::Dongle dongle;
    startDongle(dongle);
    TinyUSBDevice.isMounted = false;

    fromHost(request(SysExCommand::GET_MAC));

    REQUIRE(g_dongle_usb_midi.packets.empty());
}

TEST_CASE("sysExHandler() lets a sketch answer more commands", "[dongle][sysex]")
{
    enomik::Dongle dongle;
    startDongle(dongle);
    dongle.sysExHandler().setOnGetName([&dongle]()
                                       { dongle.sysExHandler().sendNameResponse(SysExCommand::GET_NAME_RESPONSE, "dongle"); });

    fromHost(request(SysExCommand::GET_NAME));

    const auto replies = toHost();
    REQUIRE(replies.size() == 1);
    REQUIRE(command(replies[0]) == static_cast<uint8_t>(SysExCommand::GET_NAME_RESPONSE));
}
