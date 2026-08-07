#pragma once

#include "./enomik_pinconfig.h"
#include "./version.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace enomik
{
    // Protocol version - uses library version for compatibility
    static constexpr uint8_t PROTOCOL_VERSION_MAJOR = ESP_NOW_MIDI_VERSION_MAJOR;
    static constexpr uint8_t PROTOCOL_VERSION_MINOR = ESP_NOW_MIDI_VERSION_MINOR;

    enum class SysExCommand : uint8_t
    {
        SET_PIN_CONFIG = 0x01,
        GET_PIN_CONFIG = 0x02,
        CLEAR_PIN_CONFIGS = 0x03,
        GET_ALL_PIN_CONFIGS = 0x04,
        DELETE_PIN_CONFIG = 0x05,
        GET_MAC = 0x06,
        ADD_PEER = 0x07,
        GET_ALL_PEERS = 0x08,
        RESET = 0x09,
        GET_VERSION = 0x0A,
        GET_PEER = 0x0B,
        GET_CONFIG = 0x0C,
        SET_MIDI_LOOPBACK = 0x0D,
        GET_MIDI_LOOPBACK = 0x0E,
        SET_POWER_SAVE = 0x0F,
        GET_POWER_SAVE = 0x10,

        // Response codes: always request command + 64 (0x40)
        SET_PIN_CONFIG_RESPONSE = 0x41,
        GET_PIN_CONFIG_RESPONSE = 0x42,
        CLEAR_PIN_CONFIGS_RESPONSE = 0x43,
        GET_ALL_PIN_CONFIGS_RESPONSE = 0x44,
        DELETE_PIN_CONFIG_RESPONSE = 0x45,
        GET_MAC_RESPONSE = 0x46,
        ADD_PEER_RESPONSE = 0x47,
        GET_ALL_PEERS_RESPONSE = 0x48,
        RESET_RESPONSE = 0x49,
        GET_VERSION_RESPONSE = 0x4A,
        GET_PEER_RESPONSE = 0x4B,
        GET_CONFIG_RESPONSE = 0x4C,
        SET_MIDI_LOOPBACK_RESPONSE = 0x4D,
        GET_MIDI_LOOPBACK_RESPONSE = 0x4E,
        SET_POWER_SAVE_RESPONSE = 0x4F,
        GET_POWER_SAVE_RESPONSE = 0x50,

        // Global error response (reserved; not request + 64). Request 0x3F is
        // reserved so its success response (0x7F) never collides with errors.
        ERROR_RESPONSE = 0x7F
    };

    enum class SysExErrorCode : uint8_t
    {
        BAD_VERSION = 0x01,
        UNKNOWN_COMMAND = 0x02,
        DECODE_FAILED = 0x03,
        PIN_NOT_FOUND = 0x04,
        NOT_READY = 0x05,
        OPERATION_FAILED = 0x06,
        PEER_NOT_FOUND = 0x07,
        PEER_TABLE_FULL = 0x08,
        PEER_ALREADY_EXISTS = 0x09,
    };

    enum class AddPeerResult : uint8_t
    {
        Success = 0,
        TableFull,
        AlreadyExists,
        OperationFailed,
        NotReady,
    };

    inline SysExErrorCode addPeerErrorCode(AddPeerResult result)
    {
        switch (result)
        {
        case AddPeerResult::TableFull:
            return SysExErrorCode::PEER_TABLE_FULL;
        case AddPeerResult::AlreadyExists:
            return SysExErrorCode::PEER_ALREADY_EXISTS;
        case AddPeerResult::NotReady:
            return SysExErrorCode::NOT_READY;
        case AddPeerResult::OperationFailed:
        default:
            return SysExErrorCode::OPERATION_FAILED;
        }
    }

    static constexpr size_t PEER_ENTRY_PAYLOAD_SIZE = 13; // index + 12 MAC nibbles

    inline constexpr SysExCommand responseCommand(SysExCommand request)
    {
        return static_cast<SysExCommand>(static_cast<uint8_t>(request) + 64);
    }

    struct SysExPacket
    {
        static constexpr uint8_t START_BYTE = 0xF0;
        static constexpr uint8_t END_BYTE = 0xF7;
        static constexpr uint8_t MANUFACTURER_ID = 0x7D;
        static constexpr size_t HEADER_SIZE = 5;     // START + MANUF_ID + MAJOR + MINOR + COMMAND
        static constexpr size_t MIN_PACKET_SIZE = 6; // HEADER + END
        static constexpr size_t MAX_DATA_SIZE = 256;

        uint8_t data[MAX_DATA_SIZE];
        uint16_t length;

        SysExPacket() : length(0) {}

        bool isValid() const
        {
            return length >= MIN_PACKET_SIZE &&
                   data[0] == START_BYTE &&
                   data[1] == MANUFACTURER_ID &&
                   data[length - 1] == END_BYTE;
        }

        uint8_t getMajorVersion() const
        {
            return length >= 3 ? data[2] : 0;
        }

        uint8_t getMinorVersion() const
        {
            return length >= 4 ? data[3] : 0;
        }

        bool isVersionCompatible() const
        {
            // Same major version = compatible
            return getMajorVersion() == PROTOCOL_VERSION_MAJOR;
        }

        SysExCommand getCommand() const
        {
            return length >= HEADER_SIZE ? static_cast<SysExCommand>(data[4]) : SysExCommand::GET_VERSION;
        }

        const uint8_t *getPayload() const
        {
            return data + HEADER_SIZE;
        }

        uint16_t getPayloadLength() const
        {
            // Exclude header and end byte
            return (length > MIN_PACKET_SIZE) ? (length - MIN_PACKET_SIZE) : 0;
        }
    };

    class SysExEncoder
    {
    public:
        // Encode a simple response with no payload
        static SysExPacket encodeSimpleResponse(SysExCommand cmd)
        {
            SysExPacket pkt;
            pkt.data[0] = SysExPacket::START_BYTE;
            pkt.data[1] = SysExPacket::MANUFACTURER_ID;
            pkt.data[2] = PROTOCOL_VERSION_MAJOR;
            pkt.data[3] = PROTOCOL_VERSION_MINOR;
            pkt.data[4] = static_cast<uint8_t>(cmd);
            pkt.data[5] = SysExPacket::END_BYTE;
            pkt.length = 6;
            return pkt;
        }

        // Encode a response with a single byte payload
        static SysExPacket encodeByteResponse(SysExCommand cmd, uint8_t byte)
        {
            SysExPacket pkt;
            pkt.data[0] = SysExPacket::START_BYTE;
            pkt.data[1] = SysExPacket::MANUFACTURER_ID;
            pkt.data[2] = PROTOCOL_VERSION_MAJOR;
            pkt.data[3] = PROTOCOL_VERSION_MINOR;
            pkt.data[4] = static_cast<uint8_t>(cmd);
            pkt.data[5] = byte;
            pkt.data[6] = SysExPacket::END_BYTE;
            pkt.length = 7;
            return pkt;
        }

        // Encode version response
        static SysExPacket encodeVersion()
        {
            SysExPacket pkt;
            pkt.data[0] = SysExPacket::START_BYTE;
            pkt.data[1] = SysExPacket::MANUFACTURER_ID;
            pkt.data[2] = PROTOCOL_VERSION_MAJOR;
            pkt.data[3] = PROTOCOL_VERSION_MINOR;
            pkt.data[4] = static_cast<uint8_t>(SysExCommand::GET_VERSION_RESPONSE);
            pkt.data[5] = PROTOCOL_VERSION_MAJOR;
            pkt.data[6] = PROTOCOL_VERSION_MINOR;
            pkt.data[7] = SysExPacket::END_BYTE;
            pkt.length = 8;
            return pkt;
        }

        static SysExPacket encodePinConfig(const PinConfig &cfg, SysExCommand responseCmd)
        {
            SysExPacket pkt;
            pkt.data[0] = SysExPacket::START_BYTE;
            pkt.data[1] = SysExPacket::MANUFACTURER_ID;
            pkt.data[2] = PROTOCOL_VERSION_MAJOR;
            pkt.data[3] = PROTOCOL_VERSION_MINOR;
            pkt.data[4] = static_cast<uint8_t>(responseCmd);
            pkt.data[5] = cfg.pin;
            pkt.data[6] = cfg.mode;
            pkt.data[7] = cfg.threshold;
            pkt.data[8] = cfg.midi_channel;
            pkt.data[9] = static_cast<uint8_t>(cfg.midi_type) / 2;
            pkt.data[10] = (cfg.midi_type == MidiStatus::MIDI_CONTROL_CHANGE) ? cfg.midi_cc : cfg.midi_note;
            pkt.data[11] = cfg.min_midi_value;
            pkt.data[12] = cfg.max_midi_value;
            pkt.data[13] = SysExPacket::END_BYTE;
            pkt.length = 14;
            return pkt;
        }

        // Encode MAC address (6 bytes -> 12 nibbles)
        static SysExPacket encodeMAC(const uint8_t mac[6])
        {
            SysExPacket pkt;
            pkt.data[0] = SysExPacket::START_BYTE;
            pkt.data[1] = SysExPacket::MANUFACTURER_ID;
            pkt.data[2] = PROTOCOL_VERSION_MAJOR;
            pkt.data[3] = PROTOCOL_VERSION_MINOR;
            pkt.data[4] = static_cast<uint8_t>(SysExCommand::GET_MAC_RESPONSE);

            int idx = 5;
            for (int i = 0; i < 6; i++)
            {
                uint8_t hi = (mac[i] >> 4) & 0x0F;
                uint8_t lo = mac[i] & 0x0F;
                pkt.data[idx++] = hi;
                pkt.data[idx++] = lo;
            }

            pkt.data[idx++] = SysExPacket::END_BYTE;
            pkt.length = idx;
            return pkt;
        }

        // Encode one peer entry: [index][12 MAC nibbles]
        static SysExPacket encodePeerEntry(uint8_t index, const uint8_t mac[6], SysExCommand responseCmd)
        {
            SysExPacket pkt;
            pkt.data[0] = SysExPacket::START_BYTE;
            pkt.data[1] = SysExPacket::MANUFACTURER_ID;
            pkt.data[2] = PROTOCOL_VERSION_MAJOR;
            pkt.data[3] = PROTOCOL_VERSION_MINOR;
            pkt.data[4] = static_cast<uint8_t>(responseCmd);
            pkt.data[5] = index;

            int idx = 6;
            for (int i = 0; i < 6; i++)
            {
                pkt.data[idx++] = (mac[i] >> 4) & 0x0F;
                pkt.data[idx++] = mac[i] & 0x0F;
            }

            pkt.data[idx++] = SysExPacket::END_BYTE;
            pkt.length = static_cast<uint16_t>(idx);
            return pkt;
        }

        // Error: F0 7D MAJOR MINOR 7F <failed_request> <error_code> [context] F7
        static SysExPacket encodeError(uint8_t failedRequest, SysExErrorCode errorCode)
        {
            SysExPacket pkt;
            pkt.data[0] = SysExPacket::START_BYTE;
            pkt.data[1] = SysExPacket::MANUFACTURER_ID;
            pkt.data[2] = PROTOCOL_VERSION_MAJOR;
            pkt.data[3] = PROTOCOL_VERSION_MINOR;
            pkt.data[4] = static_cast<uint8_t>(SysExCommand::ERROR_RESPONSE);
            pkt.data[5] = failedRequest;
            pkt.data[6] = static_cast<uint8_t>(errorCode);
            pkt.data[7] = SysExPacket::END_BYTE;
            pkt.length = 8;
            return pkt;
        }

        static SysExPacket encodeError(uint8_t failedRequest, SysExErrorCode errorCode, uint8_t context)
        {
            SysExPacket pkt;
            pkt.data[0] = SysExPacket::START_BYTE;
            pkt.data[1] = SysExPacket::MANUFACTURER_ID;
            pkt.data[2] = PROTOCOL_VERSION_MAJOR;
            pkt.data[3] = PROTOCOL_VERSION_MINOR;
            pkt.data[4] = static_cast<uint8_t>(SysExCommand::ERROR_RESPONSE);
            pkt.data[5] = failedRequest;
            pkt.data[6] = static_cast<uint8_t>(errorCode);
            pkt.data[7] = context;
            pkt.data[8] = SysExPacket::END_BYTE;
            pkt.length = 9;
            return pkt;
        }

        // Convert SysExPacket to midi_sysex_message (clamped to buffer capacity)
        static midi_sysex_message toMidiMessage(const SysExPacket &pkt)
        {
            midi_sysex_message msg;
            msg.length = pkt.length > midi_sysex_message::MAX_DATA_SIZE
                             ? midi_sysex_message::MAX_DATA_SIZE
                             : pkt.length;
            if (msg.length > 0)
                memcpy(msg.data, pkt.data, msg.length);
            return msg;
        }
    };

    class SysExDecoder
    {
    public:
        // Decode MAC address from nibbles
        static bool decodeMAC(const uint8_t *payload, uint16_t length, uint8_t mac[6])
        {
            if (length < 12)
                return false;

            for (int i = 0; i < 6; i++)
            {
                uint8_t hi = payload[i * 2];
                uint8_t lo = payload[i * 2 + 1];
                mac[i] = (hi << 4) | lo;
            }
            return true;
        }

        // Decode pin configuration
        static bool decodePinConfig(const uint8_t *payload, uint16_t length, PinConfig &cfg)
        {
            if (length < 8)
                return false;

            cfg.pin = payload[0];
            cfg.mode = payload[1];
            cfg.threshold = payload[2];
            cfg.midi_channel = payload[3];
            cfg.midi_type = static_cast<MidiStatus>(payload[4] * 2);
            cfg.midi_cc = payload[5];
            cfg.midi_note = payload[5];
            cfg.min_midi_value = payload[6];
            cfg.max_midi_value = payload[7];

            return true;
        }

        // Decode single index byte (pin number or peer index)
        static bool decodeIndex(const uint8_t *payload, uint16_t length, uint8_t &index)
        {
            if (length < 1)
                return false;
            index = payload[0];
            return true;
        }

        // Decode a boolean flag: payload must be exactly one byte, 0 or 1
        static bool decodeBoolFlag(const uint8_t *payload, uint16_t length, bool &enabled)
        {
            if (length != 1 || (payload[0] != 0 && payload[0] != 1))
                return false;
            enabled = payload[0] != 0;
            return true;
        }

        static bool decodeMidiLoopback(const uint8_t *payload, uint16_t length, bool &enabled)
        {
            return decodeBoolFlag(payload, length, enabled);
        }

        static bool decodePowerSave(const uint8_t *payload, uint16_t length, bool &enabled)
        {
            return decodeBoolFlag(payload, length, enabled);
        }

        static bool decodePin(const uint8_t *payload, uint16_t length, uint8_t &pin)
        {
            return decodeIndex(payload, length, pin);
        }

        // Decode peer entry: [index][12 MAC nibbles]
        static bool decodePeerEntry(const uint8_t *payload, uint16_t length, uint8_t &index, uint8_t mac[6])
        {
            if (length < PEER_ENTRY_PAYLOAD_SIZE)
                return false;

            index = payload[0];
            return decodeMAC(payload + 1, 12, mac);
        }
    };
}
