#include <catch2/catch_test_macros.hpp>

#include "arduino_stubs.h"
#include "enomik_sysex.h"

namespace {

constexpr uint8_t kSampleMac[6] = {0x84, 0xF7, 0x03, 0xF2, 0x54, 0x62};
constexpr uint8_t kOtherMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

void requireVersionHeader(const enomik::SysExPacket &pkt)
{
    REQUIRE(pkt.isValid());
    REQUIRE(pkt.length >= enomik::SysExPacket::MIN_PACKET_SIZE);
    REQUIRE(pkt.data[0] == enomik::SysExPacket::START_BYTE);
    REQUIRE(pkt.data[1] == enomik::SysExPacket::MANUFACTURER_ID);
    REQUIRE(pkt.data[2] == enomik::PROTOCOL_VERSION_MAJOR);
    REQUIRE(pkt.data[3] == enomik::PROTOCOL_VERSION_MINOR);
    REQUIRE(pkt.data[pkt.length - 1] == enomik::SysExPacket::END_BYTE);
}

void requireCommand(const enomik::SysExPacket &pkt, enomik::SysExCommand cmd)
{
    requireVersionHeader(pkt);
    REQUIRE(pkt.getCommand() == cmd);
}

PinConfig samplePinConfig()
{
    PinConfig cfg(7, 0x03);
    cfg.threshold = 5;
    cfg.midi_channel = 3;
    cfg.midi_type = MidiStatus::MIDI_CONTROL_CHANGE;
    cfg.midi_cc = 42;
    cfg.min_midi_value = 10;
    cfg.max_midi_value = 100;
    return cfg;
}

void requirePinConfigPayload(const enomik::SysExPacket &pkt)
{
    REQUIRE(pkt.length == 14);
    REQUIRE(pkt.data[5] == 7);
    REQUIRE(pkt.data[6] == 0x03);
    REQUIRE(pkt.data[7] == 5);
    REQUIRE(pkt.data[8] == 3);
    REQUIRE(pkt.data[9] == static_cast<uint8_t>(MidiStatus::MIDI_CONTROL_CHANGE) / 2);
    REQUIRE(pkt.data[10] == 42);
    REQUIRE(pkt.data[11] == 10);
    REQUIRE(pkt.data[12] == 100);
}

} // namespace

TEST_CASE("SysEx protocol version matches version.h", "[sysex][protocol]")
{
    REQUIRE(enomik::PROTOCOL_VERSION_MAJOR == ESP_NOW_MIDI_VERSION_MAJOR);
    REQUIRE(enomik::PROTOCOL_VERSION_MINOR == ESP_NOW_MIDI_VERSION_MINOR);
}

TEST_CASE("response commands are request + 64", "[sysex][protocol]")
{
    const enomik::SysExCommand requests[] = {
        enomik::SysExCommand::SET_PIN_CONFIG,
        enomik::SysExCommand::GET_PIN_CONFIG,
        enomik::SysExCommand::CLEAR_PIN_CONFIGS,
        enomik::SysExCommand::GET_ALL_PIN_CONFIGS,
        enomik::SysExCommand::DELETE_PIN_CONFIG,
        enomik::SysExCommand::GET_MAC,
        enomik::SysExCommand::ADD_PEER,
        enomik::SysExCommand::GET_PEERS,
        enomik::SysExCommand::RESET,
        enomik::SysExCommand::GET_VERSION,
    };

    for (const auto request : requests)
    {
        REQUIRE(enomik::responseCommand(request) ==
                static_cast<enomik::SysExCommand>(static_cast<uint8_t>(request) + 64));
    }
}

TEST_CASE("pin config response encoding", "[sysex][response]")
{
    const auto cfg = samplePinConfig();

    SECTION("set pin config")
    {
        const auto pkt = enomik::SysExEncoder::encodePinConfig(
            cfg, enomik::SysExCommand::SET_PIN_CONFIG_RESPONSE);
        requireCommand(pkt, enomik::SysExCommand::SET_PIN_CONFIG_RESPONSE);
        requirePinConfigPayload(pkt);
    }

    SECTION("get pin config")
    {
        const auto pkt = enomik::SysExEncoder::encodePinConfig(
            cfg, enomik::SysExCommand::GET_PIN_CONFIG_RESPONSE);
        requireCommand(pkt, enomik::SysExCommand::GET_PIN_CONFIG_RESPONSE);
        requirePinConfigPayload(pkt);
    }

    SECTION("get all pin configs")
    {
        const auto pkt = enomik::SysExEncoder::encodePinConfig(
            cfg, enomik::SysExCommand::GET_ALL_PIN_CONFIGS_RESPONSE);
        requireCommand(pkt, enomik::SysExCommand::GET_ALL_PIN_CONFIGS_RESPONSE);
        requirePinConfigPayload(pkt);
    }
}

TEST_CASE("clear pin configs response encoding", "[sysex][response]")
{
    const auto pkt = enomik::SysExEncoder::encodeSimpleResponse(
        enomik::SysExCommand::CLEAR_PIN_CONFIGS_RESPONSE);
    requireCommand(pkt, enomik::SysExCommand::CLEAR_PIN_CONFIGS_RESPONSE);
    REQUIRE(pkt.length == 6);
    REQUIRE(pkt.getPayloadLength() == 0);
}

TEST_CASE("delete pin config response encoding", "[sysex][response]")
{
    const auto pkt = enomik::SysExEncoder::encodeByteResponse(
        enomik::SysExCommand::DELETE_PIN_CONFIG_RESPONSE, 12);
    requireCommand(pkt, enomik::SysExCommand::DELETE_PIN_CONFIG_RESPONSE);
    REQUIRE(pkt.length == 7);
    REQUIRE(pkt.data[5] == 12);
}

TEST_CASE("get MAC response encoding", "[sysex][response]")
{
    const auto pkt = enomik::SysExEncoder::encodeMAC(kSampleMac);
    requireCommand(pkt, enomik::SysExCommand::GET_MAC_RESPONSE);
    REQUIRE(pkt.length == 18);

    uint8_t decoded[6] = {};
    REQUIRE(enomik::SysExDecoder::decodeMAC(pkt.getPayload(), pkt.getPayloadLength(), decoded));
    for (int i = 0; i < 6; ++i)
    {
        REQUIRE(decoded[i] == kSampleMac[i]);
    }
}

TEST_CASE("add peer response encoding", "[sysex][response]")
{
    SECTION("success")
    {
        const auto pkt = enomik::SysExEncoder::encodeByteResponse(
            enomik::SysExCommand::ADD_PEER_RESPONSE, 1);
        requireCommand(pkt, enomik::SysExCommand::ADD_PEER_RESPONSE);
        REQUIRE(pkt.data[5] == 1);
    }

    SECTION("failure")
    {
        const auto pkt = enomik::SysExEncoder::encodeByteResponse(
            enomik::SysExCommand::ADD_PEER_RESPONSE, 0);
        requireCommand(pkt, enomik::SysExCommand::ADD_PEER_RESPONSE);
        REQUIRE(pkt.data[5] == 0);
    }
}

TEST_CASE("get peers response encoding", "[sysex][response]")
{
    SECTION("empty peer list")
    {
        const uint8_t *const macs[] = {};
        const auto pkt = enomik::SysExEncoder::encodePeersResponse(macs, 0);
        requireCommand(pkt, enomik::SysExCommand::GET_PEERS_RESPONSE);
        REQUIRE(pkt.length == 6);
        REQUIRE(pkt.getPayloadLength() == 0);
    }

    SECTION("single peer")
    {
        const uint8_t *const macs[] = {kSampleMac};
        const auto pkt = enomik::SysExEncoder::encodePeersResponse(macs, 1);
        requireCommand(pkt, enomik::SysExCommand::GET_PEERS_RESPONSE);
        REQUIRE(pkt.length == 18);

        uint8_t decoded[6] = {};
        REQUIRE(enomik::SysExDecoder::decodeMAC(pkt.getPayload(), 12, decoded));
        for (int i = 0; i < 6; ++i)
        {
            REQUIRE(decoded[i] == kSampleMac[i]);
        }
    }

    SECTION("multiple peers")
    {
        const uint8_t *const macs[] = {kSampleMac, kOtherMac};
        const auto pkt = enomik::SysExEncoder::encodePeersResponse(macs, 2);
        requireCommand(pkt, enomik::SysExCommand::GET_PEERS_RESPONSE);
        REQUIRE(pkt.length == 30);
        REQUIRE(pkt.getPayloadLength() == 24);
    }
}

TEST_CASE("reset response encoding", "[sysex][response]")
{
    const auto pkt = enomik::SysExEncoder::encodeSimpleResponse(
        enomik::SysExCommand::RESET_RESPONSE);
    requireCommand(pkt, enomik::SysExCommand::RESET_RESPONSE);
    REQUIRE(pkt.length == 6);
}

TEST_CASE("get version response encoding", "[sysex][response]")
{
    const auto pkt = enomik::SysExEncoder::encodeVersion();
    requireCommand(pkt, enomik::SysExCommand::GET_VERSION_RESPONSE);
    REQUIRE(pkt.length == 8);
    REQUIRE(pkt.data[5] == enomik::PROTOCOL_VERSION_MAJOR);
    REQUIRE(pkt.data[6] == enomik::PROTOCOL_VERSION_MINOR);
}

TEST_CASE("SysExPacket version compatibility", "[sysex][protocol]")
{
    enomik::SysExPacket compatible = enomik::SysExEncoder::encodeVersion();
    REQUIRE(compatible.isVersionCompatible());

    enomik::SysExPacket incompatible = compatible;
    incompatible.data[2] = enomik::PROTOCOL_VERSION_MAJOR + 1;
    REQUIRE_FALSE(incompatible.isVersionCompatible());
}

TEST_CASE("toMidiMessage copies full packet", "[sysex][protocol]")
{
    const auto pkt = enomik::SysExEncoder::encodeMAC(kSampleMac);
    const auto msg = enomik::SysExEncoder::toMidiMessage(pkt);

    REQUIRE(msg.length == pkt.length);
    for (uint16_t i = 0; i < pkt.length; ++i)
    {
        REQUIRE(msg.data[i] == pkt.data[i]);
    }
}
