#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <vector>

#include "arduino_stubs.h"
#include "enomik_sysex.h"

namespace {

constexpr uint8_t kSampleMac[6] = {0x84, 0xF7, 0x03, 0xF2, 0x54, 0x62};

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

void requirePinConfigEqual(const PinConfig &actual, const PinConfig &expected)
{
    REQUIRE(actual.pin == expected.pin);
    REQUIRE(actual.mode == expected.mode);
    REQUIRE(actual.threshold == expected.threshold);
    REQUIRE(actual.midi_channel == expected.midi_channel);
    REQUIRE(actual.midi_type == expected.midi_type);
    REQUIRE(actual.min_midi_value == expected.min_midi_value);
    REQUIRE(actual.max_midi_value == expected.max_midi_value);

    const uint8_t wireMidiByte = (expected.midi_type == MidiStatus::MIDI_CONTROL_CHANGE)
                                     ? expected.midi_cc
                                     : expected.midi_note;
    REQUIRE(actual.midi_cc == wireMidiByte);
    REQUIRE(actual.midi_note == wireMidiByte);
}

enomik::SysExPacket makeRequestPacket(enomik::SysExCommand cmd,
                                      const uint8_t *payload,
                                      uint16_t payloadLen)
{
    enomik::SysExPacket pkt{};
    pkt.data[0] = enomik::SysExPacket::START_BYTE;
    pkt.data[1] = enomik::SysExPacket::MANUFACTURER_ID;
    pkt.data[2] = enomik::PROTOCOL_VERSION_MAJOR;
    pkt.data[3] = enomik::PROTOCOL_VERSION_MINOR;
    pkt.data[4] = static_cast<uint8_t>(cmd);
    if (payloadLen > 0 && payload != nullptr)
    {
        memcpy(pkt.data + enomik::SysExPacket::HEADER_SIZE, payload, payloadLen);
    }
    pkt.data[enomik::SysExPacket::HEADER_SIZE + payloadLen] = enomik::SysExPacket::END_BYTE;
    pkt.length = static_cast<uint16_t>(enomik::SysExPacket::HEADER_SIZE + payloadLen + 1);
    return pkt;
}

} // namespace

TEST_CASE("SysExHandler decodes incoming request packets", "[sysex][decode][handler]")
{
    enomik::SysExHandler handler;
    std::vector<midi_sysex_message> sent;

    handler.setOnSend([&sent](const midi_sysex_message &msg) {
        sent.push_back(msg);
    });

    SECTION("SET_PIN_CONFIG invokes callback with decoded config")
    {
        const auto expected = samplePinConfig();
        const auto request = enomik::SysExEncoder::encodePinConfig(
            expected, enomik::SysExCommand::SET_PIN_CONFIG);

        PinConfig received(0, 0);
        bool called = false;
        handler.setOnSetPinConfig([&](const PinConfig &cfg) {
            received = cfg;
            called = true;
        });

        handler.handleSysEx(request.data, request.length);

        REQUIRE(called);
        requirePinConfigEqual(received, expected);
        REQUIRE(sent.empty());
    }

    SECTION("GET_PIN_CONFIG invokes callback with decoded pin")
    {
        const auto request = enomik::SysExEncoder::encodeByteResponse(
            enomik::SysExCommand::GET_PIN_CONFIG, 7);

        uint8_t receivedPin = 0;
        bool called = false;
        handler.setOnGetPinConfig([&](uint8_t pin) {
            receivedPin = pin;
            called = true;
        });

        handler.handleSysEx(request.data, request.length);

        REQUIRE(called);
        REQUIRE(receivedPin == 7);
        REQUIRE(sent.empty());
    }

    SECTION("ADD_PEER invokes callback with decoded MAC")
    {
        const auto macPkt = enomik::SysExEncoder::encodeMAC(kSampleMac);
        const auto request = makeRequestPacket(
            enomik::SysExCommand::ADD_PEER,
            macPkt.getPayload(),
            macPkt.getPayloadLength());

        uint8_t receivedMac[6] = {};
        bool called = false;
        handler.setOnAddPeer([&](const uint8_t mac[6]) -> enomik::AddPeerResult {
            memcpy(receivedMac, mac, 6);
            called = true;
            return enomik::AddPeerResult::Success;
        });

        handler.handleSysEx(request.data, request.length);

        REQUIRE(called);
        for (int i = 0; i < 6; ++i)
        {
            REQUIRE(receivedMac[i] == kSampleMac[i]);
        }
        REQUIRE(sent.size() == 1);
        REQUIRE(sent[0].data[4] == static_cast<uint8_t>(enomik::SysExCommand::ADD_PEER_RESPONSE));
        REQUIRE(sent[0].data[5] == 1);
    }

    SECTION("ADD_PEER failure sends specific error")
    {
        const auto macPkt = enomik::SysExEncoder::encodeMAC(kSampleMac);
        const auto request = makeRequestPacket(
            enomik::SysExCommand::ADD_PEER,
            macPkt.getPayload(),
            macPkt.getPayloadLength());

        handler.setOnAddPeer([&](const uint8_t mac[6]) -> enomik::AddPeerResult {
            (void)mac;
            return enomik::AddPeerResult::AlreadyExists;
        });

        handler.handleSysEx(request.data, request.length);

        REQUIRE(sent.size() == 1);
        REQUIRE(sent[0].data[4] == static_cast<uint8_t>(enomik::SysExCommand::ERROR_RESPONSE));
        REQUIRE(sent[0].data[5] == static_cast<uint8_t>(enomik::SysExCommand::ADD_PEER));
        REQUIRE(sent[0].data[6] == static_cast<uint8_t>(enomik::SysExErrorCode::PEER_ALREADY_EXISTS));
    }

    SECTION("truncated packet sends DECODE_FAILED error")
    {
        handler.setOnGetPinConfig([](uint8_t) {});

        const uint8_t truncated[] = {
            enomik::SysExPacket::START_BYTE,
            enomik::SysExPacket::MANUFACTURER_ID,
            enomik::PROTOCOL_VERSION_MAJOR,
            enomik::PROTOCOL_VERSION_MINOR,
        };

        handler.handleSysEx(truncated, sizeof(truncated));

        REQUIRE(sent.size() == 1);
        REQUIRE(sent[0].length == 8);
        REQUIRE(sent[0].data[4] == static_cast<uint8_t>(enomik::SysExCommand::ERROR_RESPONSE));
        REQUIRE(sent[0].data[5] == 0);
        REQUIRE(sent[0].data[6] == static_cast<uint8_t>(enomik::SysExErrorCode::DECODE_FAILED));
    }

    SECTION("bad major version sends BAD_VERSION error")
    {
        handler.setOnGetVersion([]() {});

        auto request = enomik::SysExEncoder::encodeSimpleResponse(
            enomik::SysExCommand::GET_VERSION);
        request.data[2] = enomik::PROTOCOL_VERSION_MAJOR + 1;

        handler.handleSysEx(request.data, request.length);

        REQUIRE(sent.size() == 1);
        REQUIRE(sent[0].data[4] == static_cast<uint8_t>(enomik::SysExCommand::ERROR_RESPONSE));
        REQUIRE(sent[0].data[5] == static_cast<uint8_t>(enomik::SysExCommand::GET_VERSION));
        REQUIRE(sent[0].data[6] == static_cast<uint8_t>(enomik::SysExErrorCode::BAD_VERSION));
    }

    SECTION("unknown command sends UNKNOWN_COMMAND error")
    {
        auto request = enomik::SysExEncoder::encodeSimpleResponse(
            static_cast<enomik::SysExCommand>(0x3E));

        handler.handleSysEx(request.data, request.length);

        REQUIRE(sent.size() == 1);
        REQUIRE(sent[0].data[4] == static_cast<uint8_t>(enomik::SysExCommand::ERROR_RESPONSE));
        REQUIRE(sent[0].data[5] == 0x3E);
        REQUIRE(sent[0].data[6] == static_cast<uint8_t>(enomik::SysExErrorCode::UNKNOWN_COMMAND));
    }

    SECTION("GET_ALL_PEERS callback streams peer entries and end marker")
    {
        bool called = false;
        handler.setOnGetAllPeers([&]() {
            called = true;
            handler.sendPeerResponse(0, kSampleMac, enomik::SysExCommand::GET_ALL_PEERS_RESPONSE);
            handler.sendStreamEnd(enomik::SysExCommand::GET_ALL_PEERS_RESPONSE);
        });

        const auto request = enomik::SysExEncoder::encodeSimpleResponse(
            enomik::SysExCommand::GET_ALL_PEERS);
        handler.handleSysEx(request.data, request.length);

        REQUIRE(called);
        REQUIRE(sent.size() == 2);
        REQUIRE(sent[0].data[4] == static_cast<uint8_t>(enomik::SysExCommand::GET_ALL_PEERS_RESPONSE));
        REQUIRE(sent[0].length == 19);
        REQUIRE(sent[1].data[4] == static_cast<uint8_t>(enomik::SysExCommand::GET_ALL_PEERS_RESPONSE));
        REQUIRE(sent[1].length == 6);
    }

    SECTION("GET_PEER invokes callback and sends peer entry")
    {
        const auto request = enomik::SysExEncoder::encodeByteResponse(
            enomik::SysExCommand::GET_PEER, 1);

        uint8_t receivedIndex = 255;
        bool called = false;
        handler.setOnGetPeer([&](uint8_t index) {
            receivedIndex = index;
            called = true;
            handler.sendPeerResponse(index, kSampleMac, enomik::SysExCommand::GET_PEER_RESPONSE);
        });

        handler.handleSysEx(request.data, request.length);

        REQUIRE(called);
        REQUIRE(receivedIndex == 1);
        REQUIRE(sent.size() == 1);
        REQUIRE(sent[0].data[4] == static_cast<uint8_t>(enomik::SysExCommand::GET_PEER_RESPONSE));
        REQUIRE(sent[0].data[5] == 1);
    }

    SECTION("GET_PEER missing peer sends PEER_NOT_FOUND error")
    {
        handler.setOnGetPeer([&](uint8_t index) {
            handler.sendErrorResponse(
                static_cast<uint8_t>(enomik::SysExCommand::GET_PEER),
                enomik::SysExErrorCode::PEER_NOT_FOUND,
                index);
        });

        const auto request = enomik::SysExEncoder::encodeByteResponse(
            enomik::SysExCommand::GET_PEER, 3);
        handler.handleSysEx(request.data, request.length);

        REQUIRE(sent.size() == 1);
        REQUIRE(sent[0].data[4] == static_cast<uint8_t>(enomik::SysExCommand::ERROR_RESPONSE));
        REQUIRE(sent[0].data[5] == static_cast<uint8_t>(enomik::SysExCommand::GET_PEER));
        REQUIRE(sent[0].data[6] == static_cast<uint8_t>(enomik::SysExErrorCode::PEER_NOT_FOUND));
        REQUIRE(sent[0].data[7] == 3);
    }

    SECTION("GET_CONFIG streams pins, peers, then completion")
    {
        handler.setOnGetConfig([&]() {
            PinConfig cfg(3, 0x02);
            handler.sendPinConfigResponse(cfg, enomik::SysExCommand::GET_ALL_PIN_CONFIGS_RESPONSE);
            handler.sendPeerResponse(0, kSampleMac, enomik::SysExCommand::GET_ALL_PEERS_RESPONSE);
            handler.sendSimpleResponse(enomik::SysExCommand::GET_CONFIG_RESPONSE);
        });

        const auto request = enomik::SysExEncoder::encodeSimpleResponse(
            enomik::SysExCommand::GET_CONFIG);
        handler.handleSysEx(request.data, request.length);

        REQUIRE(sent.size() == 3);
        REQUIRE(sent[0].data[4] == static_cast<uint8_t>(enomik::SysExCommand::GET_ALL_PIN_CONFIGS_RESPONSE));
        REQUIRE(sent[1].data[4] == static_cast<uint8_t>(enomik::SysExCommand::GET_ALL_PEERS_RESPONSE));
        REQUIRE(sent[2].data[4] == static_cast<uint8_t>(enomik::SysExCommand::GET_CONFIG_RESPONSE));
        REQUIRE(sent[2].length == 6);
    }
}
