#include "./enomik_pinconfig.h"
#include "./version.h"
#include "./utils/log.h"

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
        GET_PEERS = 0x08,
        RESET = 0x09,
        GET_VERSION = 0x0A,

        // Response codes (command + 64)
        GET_PIN_CONFIG_RESPONSE = 0x42,      // 66
        GET_ALL_PIN_CONFIGS_RESPONSE = 0x44, // 68
        ADD_PEER_RESPONSE = 0x47,            // 71
        GET_PEERS_RESPONSE = 0x48,           // 72
        RESET_RESPONSE = 0x49,               // 73 (RESET + 64)
        GET_VERSION_RESPONSE = 0x4A          // 74
    };

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

        static SysExPacket encodePinConfig(const PinConfig &cfg)
        {
            SysExPacket pkt;
            pkt.data[0] = SysExPacket::START_BYTE;
            pkt.data[1] = SysExPacket::MANUFACTURER_ID;
            pkt.data[2] = PROTOCOL_VERSION_MAJOR;
            pkt.data[3] = PROTOCOL_VERSION_MINOR;
            pkt.data[4] = static_cast<uint8_t>(SysExCommand::GET_PIN_CONFIG_RESPONSE);
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
            pkt.data[4] = static_cast<uint8_t>(SysExCommand::GET_MAC);

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

        // Convert SysExPacket to midi_sysex_message
        static midi_sysex_message toMidiMessage(const SysExPacket &pkt)
        {
            midi_sysex_message msg;
            msg.length = pkt.length;
            memcpy(msg.data, pkt.data, pkt.length);
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

            enomik_log_debug("Decoding PinConfig: ch=%d type=0x%02X cc/note=%d min=%d max=%d",
                payload[3], payload[4], payload[5], payload[6], payload[7]);

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

        // Decode single pin number
        static bool decodePin(const uint8_t *payload, uint16_t length, uint8_t &pin)
        {
            if (length < 1)
                return false;
            pin = payload[0];
            return true;
        }
    };

    class SysExHandler
    {
    public:
        // Callbacks for different SysEx commands
        using PinConfigCallback = std::function<void(const PinConfig &)>;
        using PinQueryCallback = std::function<void(uint8_t pin)>;
        using VoidCallback = std::function<void()>;
        using MACCallback = std::function<void(const uint8_t mac[6])>;
        using SendCallback = std::function<void(const midi_sysex_message &)>;

        void setOnSetPinConfig(PinConfigCallback cb) { _onSetPinConfig = cb; }
        void setOnGetPinConfig(PinQueryCallback cb) { _onGetPinConfig = cb; }
        void setOnDeletePinConfig(PinQueryCallback cb) { _onDeletePinConfig = cb; }
        void setOnClearPinConfigs(VoidCallback cb) { _onClearPinConfigs = cb; }
        void setOnGetAllPinConfigs(VoidCallback cb) { _onGetAllPinConfigs = cb; }
        void setOnGetMAC(VoidCallback cb) { _onGetMAC = cb; }
        void setOnAddPeer(MACCallback cb) { _onAddPeer = cb; }
        void setOnGetPeers(VoidCallback cb) { _onGetPeers = cb; }
        void setOnReset(VoidCallback cb) { _onReset = cb; }
        void setOnGetVersion(VoidCallback cb) { _onGetVersion = cb; }
        void setOnSend(SendCallback cb) { _onSend = cb; }

        // Main entry point for handling incoming SysEx messages
        void handleSysEx(const uint8_t *data, uint16_t length)
        {
            // Validate minimum length
            if (length < SysExPacket::MIN_PACKET_SIZE)
            {
                enomik_log_error("SysEx: Invalid packet length");
                return;
            }

            // Create packet wrapper
            SysExPacket packet;
            packet.length = length;
            memcpy(packet.data, data, length);

            if (!packet.isValid())
            {
                enomik_log_error("SysEx: Invalid packet format");
                return;
            }

            // Check version compatibility
            if (!packet.isVersionCompatible())
            {
                enomik_log_error("SysEx: Incompatible protocol version %d.%d (expected %d.x)",
                    packet.getMajorVersion(), packet.getMinorVersion(), PROTOCOL_VERSION_MAJOR);
                return;
            }

            // Route to appropriate handler
            routeCommand(packet);
        }

        // Send responses
        void sendPinConfigResponse(const PinConfig &cfg)
        {
            if (!_onSend)
                return;

            SysExPacket pkt = SysExEncoder::encodePinConfig(cfg);
            midi_sysex_message msg = SysExEncoder::toMidiMessage(pkt);
            _onSend(msg);
        }

        void sendMACResponse(const uint8_t mac[6])
        {
            if (!_onSend)
                return;

            SysExPacket pkt = SysExEncoder::encodeMAC(mac);
            midi_sysex_message msg = SysExEncoder::toMidiMessage(pkt);
            _onSend(msg);
        }

        void sendVersionResponse()
        {
            if (!_onSend)
                return;

            SysExPacket pkt = SysExEncoder::encodeVersion();
            midi_sysex_message msg = SysExEncoder::toMidiMessage(pkt);
            _onSend(msg);
        }

        void sendSimpleResponse(SysExCommand cmd)
        {
            if (!_onSend)
                return;

            SysExPacket pkt = SysExEncoder::encodeSimpleResponse(cmd);
            midi_sysex_message msg = SysExEncoder::toMidiMessage(pkt);
            _onSend(msg);
        }

        void sendDeleteResponse(uint8_t pin)
        {
            if (!_onSend)
                return;

            SysExPacket pkt = SysExEncoder::encodeByteResponse(
                SysExCommand::DELETE_PIN_CONFIG, pin);
            midi_sysex_message msg = SysExEncoder::toMidiMessage(pkt);
            _onSend(msg);
        }

        void sendAddPeerResponse(bool success)
        {
            if (!_onSend)
                return;

            SysExPacket pkt = SysExEncoder::encodeByteResponse(
                SysExCommand::ADD_PEER_RESPONSE, success ? 1 : 0);
            midi_sysex_message msg = SysExEncoder::toMidiMessage(pkt);
            _onSend(msg);
        }

    private:
        PinConfigCallback _onSetPinConfig;
        PinQueryCallback _onGetPinConfig;
        PinQueryCallback _onDeletePinConfig;
        VoidCallback _onClearPinConfigs;
        VoidCallback _onGetAllPinConfigs;
        VoidCallback _onGetMAC;
        MACCallback _onAddPeer;
        VoidCallback _onGetPeers;
        VoidCallback _onReset;
        VoidCallback _onGetVersion;
        SendCallback _onSend;

        void routeCommand(const SysExPacket &packet)
        {
            SysExCommand cmd = packet.getCommand();
            const uint8_t *payload = packet.getPayload();
            uint16_t payloadLen = packet.getPayloadLength();

            enomik_log_debug("SysEx: Handling command 0x%02X", static_cast<uint8_t>(cmd));

            switch (cmd)
            {
            case SysExCommand::SET_PIN_CONFIG:
                handleSetPinConfig(payload, payloadLen);
                break;

            case SysExCommand::GET_PIN_CONFIG:
                handleGetPinConfig(payload, payloadLen);
                break;

            case SysExCommand::DELETE_PIN_CONFIG:
                handleDeletePinConfig(payload, payloadLen);
                break;

            case SysExCommand::CLEAR_PIN_CONFIGS:
                handleClearPinConfigs();
                break;

            case SysExCommand::GET_ALL_PIN_CONFIGS:
                handleGetAllPinConfigs();
                break;

            case SysExCommand::GET_MAC:
                handleGetMAC();
                break;

            case SysExCommand::ADD_PEER:
                handleAddPeer(payload, payloadLen);
                break;

            case SysExCommand::GET_PEERS:
                handleGetPeers();
                break;

            case SysExCommand::RESET:
                handleReset();
                break;

            case SysExCommand::GET_VERSION:
                handleGetVersion();
                break;

            default:
                enomik_log_error("SysEx: Unknown command: 0x%02X", static_cast<uint8_t>(cmd));
                break;
            }
        }

        void handleSetPinConfig(const uint8_t *payload, uint16_t length)
        {
            if (!_onSetPinConfig)
            {
                enomik_log_debug("SysEx: No SET_PIN_CONFIG handler");
                return;
            }

            PinConfig cfg(0, 0);
            if (SysExDecoder::decodePinConfig(payload, length, cfg))
            {
                enomik_log_debug("SysEx: Setting config for pin %d", cfg.pin);
                _onSetPinConfig(cfg);
            }
            else
            {
                enomik_log_error("SysEx: Failed to decode pin config");
            }
        }

        void handleGetPinConfig(const uint8_t *payload, uint16_t length)
        {
            if (!_onGetPinConfig)
            {
                enomik_log_debug("SysEx: No GET_PIN_CONFIG handler");
                return;
            }

            uint8_t pin;
            if (SysExDecoder::decodePin(payload, length, pin))
            {
                enomik_log_debug("SysEx: Getting config for pin %d", pin);
                _onGetPinConfig(pin);
            }
            else
            {
                enomik_log_error("SysEx: Failed to decode pin number");
            }
        }

        void handleDeletePinConfig(const uint8_t *payload, uint16_t length)
        {
            if (!_onDeletePinConfig)
            {
                enomik_log_debug("SysEx: No DELETE_PIN_CONFIG handler");
                return;
            }

            uint8_t pin;
            if (SysExDecoder::decodePin(payload, length, pin))
            {
                enomik_log_debug("SysEx: Deleting config for pin %d", pin);
                _onDeletePinConfig(pin);
            }
            else
            {
                enomik_log_error("SysEx: Failed to decode pin number");
            }
        }

        void handleClearPinConfigs()
        {
            if (_onClearPinConfigs)
            {
                enomik_log_debug("SysEx: Clearing all pin configs");
                _onClearPinConfigs();
            }
        }

        void handleGetAllPinConfigs()
        {
            if (_onGetAllPinConfigs)
            {
                enomik_log_debug("SysEx: Getting all pin configs");
                _onGetAllPinConfigs();
            }
        }

        void handleGetMAC()
        {
            if (_onGetMAC)
            {
                enomik_log_debug("SysEx: Getting MAC address");
                _onGetMAC();
            }
        }

        void handleAddPeer(const uint8_t *payload, uint16_t length)
        {
            if (!_onAddPeer)
            {
                enomik_log_debug("SysEx: No ADD_PEER handler");
                return;
            }

            uint8_t mac[6];
            if (SysExDecoder::decodeMAC(payload, length, mac))
            {
                enomik_log_debug("SysEx: Adding peer: %02X:%02X:%02X:%02X:%02X:%02X",
                    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
                _onAddPeer(mac);
            }
            else
            {
                enomik_log_error("SysEx: Failed to decode MAC address");
            }
        }

        void handleGetPeers()
        {
            if (_onGetPeers)
            {
                enomik_log_debug("SysEx: Getting peers");
                _onGetPeers();
            }
        }

        void handleReset()
        {
            if (_onReset)
            {
                enomik_log_debug("SysEx: Performing reset");
                _onReset();
            }
        }

        void handleGetVersion()
        {
            if (_onGetVersion)
            {
                enomik_log_debug("SysEx: Getting version");
                _onGetVersion();
            }
        }
    };
};