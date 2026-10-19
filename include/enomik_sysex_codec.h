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
        SET_NAME = 0x11,
        GET_NAME = 0x12,
        // 0x13, 0x14 reserved
        ANNOUNCE = 0x15, // device -> device notification, never answered
        SET_RAW_STREAM = 0x16,
        RAW_VALUE = 0x17, // device -> host notification while raw stream is on, never answered

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
        SET_NAME_RESPONSE = 0x51,
        GET_NAME_RESPONSE = 0x52,
        SET_RAW_STREAM_RESPONSE = 0x56,

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

    enum class DeviceKind : uint8_t
    {
        CLIENT = 0x00,
        DONGLE = 0x01,
    };

    // Client name: printable ASCII (0x20-0x7E), 7-bit SysEx safe without nibble encoding
    static constexpr size_t MAX_NAME_LENGTH = 16;

    inline constexpr bool isNameChar(uint8_t c)
    {
        return c >= 0x20 && c <= 0x7E;
    }

    /** Who a device is: sent in ANNOUNCE at boot, after pairing and as heartbeat. */
    struct Announce
    {
        DeviceKind kind = DeviceKind::CLIENT;
        uint8_t major = 0;
        uint8_t minor = 0;
        uint8_t patch = 0;
        char name[MAX_NAME_LENGTH + 1] = {};
    };

    static constexpr size_t ANNOUNCE_HEADER_SIZE = 4; // kind + major + minor + patch

    /** SET_RAW_STREAM pin value that streams every configured input pin. */
    static constexpr uint8_t RAW_STREAM_ALL_PINS = 0x7F;

    /** SET_RAW_STREAM request: [pin][enable][interval in 5 ms units, 0 = default]. */
    struct RawStreamRequest
    {
        uint8_t pin = RAW_STREAM_ALL_PINS;
        bool enable = false;
        uint8_t interval = 0;
    };

    /** One RAW_VALUE frame: live reading of an input pin. */
    struct RawValue
    {
        uint8_t pin = 0;
        uint8_t mode = 0;
        uint32_t raw = 0;      // touchRead() / analogRead()
        uint32_t baseline = 0; // untouched touch reading; 0 for analog
        uint32_t peak = 0;     // learned touch delta that maps to 127; 0 for analog
        uint8_t percent = 0;   // touch: change from baseline in % (max 127); analog: 0
        uint8_t norm = 0;      // reading normalized to 0-127
        uint8_t threshold = 0;
        bool active = false;   // touch threshold mode: currently touched
    };

    static constexpr size_t SEPTET32_SIZE = 5;
    static constexpr size_t RAW_VALUE_PAYLOAD_SIZE = 2 + 3 * SEPTET32_SIZE + 4;

    // 32-bit value as 5 SysEx-safe bytes, most significant first
    inline void encodeSeptets32(uint32_t value, uint8_t *out)
    {
        for (int i = SEPTET32_SIZE - 1; i >= 0; i--)
        {
            out[i] = value & 0x7F;
            value >>= 7;
        }
    }

    inline uint32_t decodeSeptets32(const uint8_t *in)
    {
        uint32_t value = 0;
        for (size_t i = 0; i < SEPTET32_SIZE; i++)
            value = (value << 7) | (in[i] & 0x7F);
        return value;
    }

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

        /** @brief True when raw SysEx (`F0` ...) carries the enomik manufacturer ID. */
        static bool isEnomik(const uint8_t *raw, size_t rawLength)
        {
            return raw && rawLength >= 2 && raw[0] == START_BYTE && raw[1] == MANUFACTURER_ID;
        }

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

        // Encode SET_RAW_STREAM request or response: [pin][enable][interval]
        static SysExPacket encodeRawStream(const RawStreamRequest &req, SysExCommand cmd)
        {
            SysExPacket pkt;
            pkt.data[0] = SysExPacket::START_BYTE;
            pkt.data[1] = SysExPacket::MANUFACTURER_ID;
            pkt.data[2] = PROTOCOL_VERSION_MAJOR;
            pkt.data[3] = PROTOCOL_VERSION_MINOR;
            pkt.data[4] = static_cast<uint8_t>(cmd);
            pkt.data[5] = req.pin & 0x7F;
            pkt.data[6] = req.enable ? 1 : 0;
            pkt.data[7] = req.interval & 0x7F;
            pkt.data[8] = SysExPacket::END_BYTE;
            pkt.length = 9;
            return pkt;
        }

        // Encode RAW_VALUE: [pin][mode][raw x5][baseline x5][peak x5][percent][norm][threshold][active]
        static SysExPacket encodeRawValue(const RawValue &v)
        {
            SysExPacket pkt;
            pkt.data[0] = SysExPacket::START_BYTE;
            pkt.data[1] = SysExPacket::MANUFACTURER_ID;
            pkt.data[2] = PROTOCOL_VERSION_MAJOR;
            pkt.data[3] = PROTOCOL_VERSION_MINOR;
            pkt.data[4] = static_cast<uint8_t>(SysExCommand::RAW_VALUE);
            uint8_t *p = pkt.data + SysExPacket::HEADER_SIZE;
            *p++ = v.pin & 0x7F;
            *p++ = v.mode & 0x7F;
            encodeSeptets32(v.raw, p);
            p += SEPTET32_SIZE;
            encodeSeptets32(v.baseline, p);
            p += SEPTET32_SIZE;
            encodeSeptets32(v.peak, p);
            p += SEPTET32_SIZE;
            *p++ = v.percent & 0x7F;
            *p++ = v.norm & 0x7F;
            *p++ = v.threshold & 0x7F;
            *p++ = v.active ? 1 : 0;
            *p++ = SysExPacket::END_BYTE;
            pkt.length = p - pkt.data;
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
            pkt.data[7] = ESP_NOW_MIDI_VERSION_PATCH;
            pkt.data[8] = SysExPacket::END_BYTE;
            pkt.length = 9;
            return pkt;
        }

        // Encode name response: [name bytes] (up to MAX_NAME_LENGTH, no terminator)
        static SysExPacket encodeName(const char *name, SysExCommand responseCmd)
        {
            SysExPacket pkt;
            pkt.data[0] = SysExPacket::START_BYTE;
            pkt.data[1] = SysExPacket::MANUFACTURER_ID;
            pkt.data[2] = PROTOCOL_VERSION_MAJOR;
            pkt.data[3] = PROTOCOL_VERSION_MINOR;
            pkt.data[4] = static_cast<uint8_t>(responseCmd);

            int idx = 5;
            for (size_t i = 0; name && name[i] && i < MAX_NAME_LENGTH; i++)
            {
                pkt.data[idx++] = static_cast<uint8_t>(name[i]) & 0x7F;
            }

            pkt.data[idx++] = SysExPacket::END_BYTE;
            pkt.length = idx;
            return pkt;
        }

        // Encode ANNOUNCE: [kind][major][minor][patch][name bytes]
        static SysExPacket encodeAnnounce(DeviceKind kind, const char *name)
        {
            SysExPacket pkt;
            pkt.data[0] = SysExPacket::START_BYTE;
            pkt.data[1] = SysExPacket::MANUFACTURER_ID;
            pkt.data[2] = PROTOCOL_VERSION_MAJOR;
            pkt.data[3] = PROTOCOL_VERSION_MINOR;
            pkt.data[4] = static_cast<uint8_t>(SysExCommand::ANNOUNCE);
            pkt.data[5] = static_cast<uint8_t>(kind);
            pkt.data[6] = PROTOCOL_VERSION_MAJOR;
            pkt.data[7] = PROTOCOL_VERSION_MINOR;
            pkt.data[8] = ESP_NOW_MIDI_VERSION_PATCH;

            int idx = 9;
            for (size_t i = 0; name && name[i] && i < MAX_NAME_LENGTH; i++)
            {
                if (isNameChar(static_cast<uint8_t>(name[i])))
                    pkt.data[idx++] = static_cast<uint8_t>(name[i]);
            }

            pkt.data[idx++] = SysExPacket::END_BYTE;
            pkt.length = idx;
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

        // Decode SET_RAW_STREAM: [pin][enable 0/1][interval]
        static bool decodeRawStream(const uint8_t *payload, uint16_t length, RawStreamRequest &req)
        {
            if (length != 3 || payload[1] > 1)
                return false;
            req.pin = payload[0];
            req.enable = payload[1] != 0;
            req.interval = payload[2];
            return true;
        }

        // Decode RAW_VALUE payload (see SysExEncoder::encodeRawValue)
        static bool decodeRawValue(const uint8_t *payload, uint16_t length, RawValue &v)
        {
            if (!payload || length != RAW_VALUE_PAYLOAD_SIZE)
                return false;
            const uint8_t *p = payload;
            v.pin = *p++;
            v.mode = *p++;
            v.raw = decodeSeptets32(p);
            p += SEPTET32_SIZE;
            v.baseline = decodeSeptets32(p);
            p += SEPTET32_SIZE;
            v.peak = decodeSeptets32(p);
            p += SEPTET32_SIZE;
            v.percent = *p++;
            v.norm = *p++;
            v.threshold = *p++;
            v.active = *p++ != 0;
            return true;
        }

        // Decode name: 0..MAX_NAME_LENGTH printable ASCII bytes. Empty = clear override.
        static bool decodeName(const uint8_t *payload, uint16_t length, char name[MAX_NAME_LENGTH + 1])
        {
            if (length > MAX_NAME_LENGTH)
                return false;

            for (uint16_t i = 0; i < length; i++)
            {
                if (!isNameChar(payload[i]))
                    return false;
                name[i] = static_cast<char>(payload[i]);
            }
            name[length] = '\0';
            return true;
        }

        // Decode ANNOUNCE payload: [kind][major][minor][patch][name bytes]
        static bool decodeAnnounce(const uint8_t *payload, uint16_t length, Announce &out)
        {
            if (!payload || length < ANNOUNCE_HEADER_SIZE)
                return false;
            if (payload[0] > static_cast<uint8_t>(DeviceKind::DONGLE))
                return false;
            if (!decodeName(payload + ANNOUNCE_HEADER_SIZE, length - ANNOUNCE_HEADER_SIZE, out.name))
                return false;
            out.kind = static_cast<DeviceKind>(payload[0]);
            out.major = payload[1];
            out.minor = payload[2];
            out.patch = payload[3];
            return true;
        }

        // Decode a complete ANNOUNCE SysEx message (F0 7D major minor 0x15 ... F7)
        static bool decodeAnnounceMessage(const uint8_t *data, uint16_t length, Announce &out)
        {
            if (!data || length < SysExPacket::MIN_PACKET_SIZE + ANNOUNCE_HEADER_SIZE ||
                length > SysExPacket::MAX_DATA_SIZE)
                return false;
            if (data[0] != SysExPacket::START_BYTE || data[1] != SysExPacket::MANUFACTURER_ID ||
                data[2] != PROTOCOL_VERSION_MAJOR || data[length - 1] != SysExPacket::END_BYTE ||
                data[4] != static_cast<uint8_t>(SysExCommand::ANNOUNCE))
                return false;
            return decodeAnnounce(data + SysExPacket::HEADER_SIZE,
                                  static_cast<uint16_t>(length - SysExPacket::MIN_PACKET_SIZE), out);
        }

        // True for any enomik SysEx ANNOUNCE, even one this firmware cannot decode
        static bool isAnnounce(const uint8_t *data, uint16_t length)
        {
            return data && length >= SysExPacket::HEADER_SIZE &&
                   data[0] == SysExPacket::START_BYTE && data[1] == SysExPacket::MANUFACTURER_ID &&
                   data[4] == static_cast<uint8_t>(SysExCommand::ANNOUNCE);
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
