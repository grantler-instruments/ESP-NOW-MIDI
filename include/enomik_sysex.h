#pragma once

#include "./enomik_sysex_codec.h"
#include "./esp_now_midi_log.h"

#include <functional>

/**
 * @file enomik_sysex.h
 * @brief SysEx protocol support for configuring Enomik devices.
 */

namespace enomik
{
    /**
     * @brief Routes SysEx commands to application callbacks and sends responses.
     */
    class SysExHandler
    {
    public:
        // Callbacks for different SysEx commands
        using PinConfigCallback = std::function<void(const PinConfig &)>;
        using PinQueryCallback = std::function<void(uint8_t pin)>;
        using PeerQueryCallback = std::function<void(uint8_t index)>;
        using BoolCallback = std::function<void(bool)>;
        using VoidCallback = std::function<void()>;
        using AddPeerCallback = std::function<AddPeerResult(const uint8_t mac[6])>;
        using SendCallback = std::function<void(const midi_sysex_message &)>;

        void setOnSetPinConfig(PinConfigCallback cb) { _onSetPinConfig = cb; }
        void setOnGetPinConfig(PinQueryCallback cb) { _onGetPinConfig = cb; }
        void setOnDeletePinConfig(PinQueryCallback cb) { _onDeletePinConfig = cb; }
        void setOnClearPinConfigs(VoidCallback cb) { _onClearPinConfigs = cb; }
        void setOnGetAllPinConfigs(VoidCallback cb) { _onGetAllPinConfigs = cb; }
        void setOnGetMAC(VoidCallback cb) { _onGetMAC = cb; }
        void setOnAddPeer(AddPeerCallback cb) { _onAddPeer = cb; }
        void setOnGetAllPeers(VoidCallback cb) { _onGetAllPeers = cb; }
        void setOnGetPeer(PeerQueryCallback cb) { _onGetPeer = cb; }
        void setOnGetConfig(VoidCallback cb) { _onGetConfig = cb; }
        void setOnSetMidiLoopback(BoolCallback cb) { _onSetMidiLoopback = cb; }
        void setOnGetMidiLoopback(VoidCallback cb) { _onGetMidiLoopback = cb; }
        void setOnSetPowerSave(BoolCallback cb) { _onSetPowerSave = cb; }
        void setOnGetPowerSave(VoidCallback cb) { _onGetPowerSave = cb; }
        void setOnReset(VoidCallback cb) { _onReset = cb; }
        void setOnGetVersion(VoidCallback cb) { _onGetVersion = cb; }
        void setOnSend(SendCallback cb) { _onSend = cb; }

        // Main entry point for handling incoming SysEx messages
        void handleSysEx(const uint8_t *data, uint16_t length)
        {
            // Validate minimum length
            if (length < SysExPacket::MIN_PACKET_SIZE)
            {
                EspNowMidiLog::w("SysEx: Invalid packet length");
                sendErrorResponse(0, SysExErrorCode::DECODE_FAILED);
                return;
            }

            // Create packet wrapper
            SysExPacket packet;
            packet.length = length;
            memcpy(packet.data, data, length);

            if (!packet.isValid())
            {
                EspNowMidiLog::w("SysEx: Invalid packet format");
                sendErrorResponse(0, SysExErrorCode::DECODE_FAILED);
                return;
            }

            // Check version compatibility
            if (!packet.isVersionCompatible())
            {
                EspNowMidiLog::w("SysEx: Incompatible protocol version %u.%u (expected %u.x)",
                               packet.getMajorVersion(), packet.getMinorVersion(), PROTOCOL_VERSION_MAJOR);
                sendErrorResponse(static_cast<uint8_t>(packet.getCommand()), SysExErrorCode::BAD_VERSION);
                return;
            }

            // Route to appropriate handler
            routeCommand(packet);
        }

        void sendErrorResponse(uint8_t failedRequest, SysExErrorCode errorCode)
        {
            if (!_onSend)
                return;

            SysExPacket pkt = SysExEncoder::encodeError(failedRequest, errorCode);
            midi_sysex_message msg = SysExEncoder::toMidiMessage(pkt);
            _onSend(msg);
        }

        void sendErrorResponse(uint8_t failedRequest, SysExErrorCode errorCode, uint8_t context)
        {
            if (!_onSend)
                return;

            SysExPacket pkt = SysExEncoder::encodeError(failedRequest, errorCode, context);
            midi_sysex_message msg = SysExEncoder::toMidiMessage(pkt);
            _onSend(msg);
        }

        // Send responses
        void sendPinConfigResponse(const PinConfig &cfg, SysExCommand responseCmd)
        {
            if (!_onSend)
                return;

            SysExPacket pkt = SysExEncoder::encodePinConfig(cfg, responseCmd);
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
                SysExCommand::DELETE_PIN_CONFIG_RESPONSE, pin);
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

        void sendPeerResponse(uint8_t index, const uint8_t mac[6], SysExCommand responseCmd)
        {
            if (!_onSend)
                return;

            SysExPacket pkt = SysExEncoder::encodePeerEntry(index, mac, responseCmd);
            midi_sysex_message msg = SysExEncoder::toMidiMessage(pkt);
            _onSend(msg);
        }

        void sendStreamEnd(SysExCommand responseCmd)
        {
            sendSimpleResponse(responseCmd);
        }

        void sendByteResponse(SysExCommand cmd, uint8_t byte)
        {
            if (!_onSend)
                return;

            SysExPacket pkt = SysExEncoder::encodeByteResponse(cmd, byte);
            midi_sysex_message msg = SysExEncoder::toMidiMessage(pkt);
            _onSend(msg);
        }

        void sendMidiLoopbackResponse(SysExCommand responseCmd, bool enabled)
        {
            sendByteResponse(responseCmd, enabled ? 1 : 0);
        }

        void sendPowerSaveResponse(SysExCommand responseCmd, bool enabled)
        {
            sendByteResponse(responseCmd, enabled ? 1 : 0);
        }

    private:
        PinConfigCallback _onSetPinConfig;
        PinQueryCallback _onGetPinConfig;
        PinQueryCallback _onDeletePinConfig;
        VoidCallback _onClearPinConfigs;
        VoidCallback _onGetAllPinConfigs;
        VoidCallback _onGetMAC;
        AddPeerCallback _onAddPeer;
        VoidCallback _onGetAllPeers;
        PeerQueryCallback _onGetPeer;
        VoidCallback _onGetConfig;
        BoolCallback _onSetMidiLoopback;
        VoidCallback _onGetMidiLoopback;
        BoolCallback _onSetPowerSave;
        VoidCallback _onGetPowerSave;
        VoidCallback _onReset;
        VoidCallback _onGetVersion;
        SendCallback _onSend;

        void routeCommand(const SysExPacket &packet)
        {
            SysExCommand cmd = packet.getCommand();
            const uint8_t *payload = packet.getPayload();
            uint16_t payloadLen = packet.getPayloadLength();

            EspNowMidiLog::d("SysEx: Handling command 0x%02X", static_cast<uint8_t>(cmd));

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

            case SysExCommand::GET_ALL_PEERS:
                handleGetAllPeers();
                break;

            case SysExCommand::GET_PEER:
                handleGetPeer(payload, payloadLen);
                break;

            case SysExCommand::GET_CONFIG:
                handleGetConfig();
                break;

            case SysExCommand::SET_MIDI_LOOPBACK:
                handleSetMidiLoopback(payload, payloadLen);
                break;

            case SysExCommand::GET_MIDI_LOOPBACK:
                handleGetMidiLoopback();
                break;

            case SysExCommand::SET_POWER_SAVE:
                handleSetPowerSave(payload, payloadLen);
                break;

            case SysExCommand::GET_POWER_SAVE:
                handleGetPowerSave();
                break;

            case SysExCommand::RESET:
                handleReset();
                break;

            case SysExCommand::GET_VERSION:
                handleGetVersion();
                break;

            default:
                EspNowMidiLog::w("SysEx: Unknown command: 0x%02X", static_cast<uint8_t>(cmd));
                sendErrorResponse(static_cast<uint8_t>(cmd), SysExErrorCode::UNKNOWN_COMMAND);
                break;
            }
        }

        void handleSetPinConfig(const uint8_t *payload, uint16_t length)
        {
            if (!_onSetPinConfig)
            {
                EspNowMidiLog::w("SysEx: No SET_PIN_CONFIG handler");
                sendErrorResponse(static_cast<uint8_t>(SysExCommand::SET_PIN_CONFIG), SysExErrorCode::NOT_READY);
                return;
            }

            PinConfig cfg(0, 0);
            if (SysExDecoder::decodePinConfig(payload, length, cfg))
            {
                EspNowMidiLog::d("SysEx: Setting config for pin %u", cfg.pin);
                _onSetPinConfig(cfg);
            }
            else
            {
                EspNowMidiLog::w("SysEx: Failed to decode pin config");
                sendErrorResponse(static_cast<uint8_t>(SysExCommand::SET_PIN_CONFIG), SysExErrorCode::DECODE_FAILED);
            }
        }

        void handleGetPinConfig(const uint8_t *payload, uint16_t length)
        {
            if (!_onGetPinConfig)
            {
                EspNowMidiLog::w("SysEx: No GET_PIN_CONFIG handler");
                sendErrorResponse(static_cast<uint8_t>(SysExCommand::GET_PIN_CONFIG), SysExErrorCode::NOT_READY);
                return;
            }

            uint8_t pin;
            if (SysExDecoder::decodePin(payload, length, pin))
            {
                EspNowMidiLog::d("SysEx: Getting config for pin %u", pin);
                _onGetPinConfig(pin);
            }
            else
            {
                EspNowMidiLog::w("SysEx: Failed to decode pin number");
                sendErrorResponse(static_cast<uint8_t>(SysExCommand::GET_PIN_CONFIG), SysExErrorCode::DECODE_FAILED);
            }
        }

        void handleDeletePinConfig(const uint8_t *payload, uint16_t length)
        {
            if (!_onDeletePinConfig)
            {
                EspNowMidiLog::w("SysEx: No DELETE_PIN_CONFIG handler");
                sendErrorResponse(static_cast<uint8_t>(SysExCommand::DELETE_PIN_CONFIG), SysExErrorCode::NOT_READY);
                return;
            }

            uint8_t pin;
            if (SysExDecoder::decodePin(payload, length, pin))
            {
                EspNowMidiLog::d("SysEx: Deleting config for pin %u", pin);
                _onDeletePinConfig(pin);
            }
            else
            {
                EspNowMidiLog::w("SysEx: Failed to decode pin number");
                sendErrorResponse(static_cast<uint8_t>(SysExCommand::DELETE_PIN_CONFIG), SysExErrorCode::DECODE_FAILED);
            }
        }

        void handleClearPinConfigs()
        {
            if (_onClearPinConfigs)
            {
                EspNowMidiLog::d("SysEx: Clearing all pin configs");
                _onClearPinConfigs();
            }
            else
            {
                sendErrorResponse(static_cast<uint8_t>(SysExCommand::CLEAR_PIN_CONFIGS), SysExErrorCode::NOT_READY);
            }
        }

        void handleGetAllPinConfigs()
        {
            if (_onGetAllPinConfigs)
            {
                EspNowMidiLog::d("SysEx: Getting all pin configs");
                _onGetAllPinConfigs();
            }
            else
            {
                sendErrorResponse(static_cast<uint8_t>(SysExCommand::GET_ALL_PIN_CONFIGS), SysExErrorCode::NOT_READY);
            }
        }

        void handleGetMAC()
        {
            if (_onGetMAC)
            {
                EspNowMidiLog::d("SysEx: Getting MAC address");
                _onGetMAC();
            }
            else
            {
                sendErrorResponse(static_cast<uint8_t>(SysExCommand::GET_MAC), SysExErrorCode::NOT_READY);
            }
        }

        void handleAddPeer(const uint8_t *payload, uint16_t length)
        {
            if (!_onAddPeer)
            {
                EspNowMidiLog::w("SysEx: No ADD_PEER handler");
                sendErrorResponse(static_cast<uint8_t>(SysExCommand::ADD_PEER), SysExErrorCode::NOT_READY);
                return;
            }

            uint8_t mac[6];
            if (SysExDecoder::decodeMAC(payload, length, mac))
            {
                EspNowMidiLog::mac("SysEx: Adding peer: ", mac);
                const AddPeerResult result = _onAddPeer(mac);
                if (result == AddPeerResult::Success)
                {
                    sendAddPeerResponse(true);
                }
                else
                {
                    sendErrorResponse(
                        static_cast<uint8_t>(SysExCommand::ADD_PEER),
                        addPeerErrorCode(result));
                }
            }
            else
            {
                EspNowMidiLog::w("SysEx: Failed to decode MAC address");
                sendErrorResponse(static_cast<uint8_t>(SysExCommand::ADD_PEER), SysExErrorCode::DECODE_FAILED);
            }
        }

        void handleGetAllPeers()
        {
            if (_onGetAllPeers)
            {
                EspNowMidiLog::d("SysEx: Getting all peers");
                _onGetAllPeers();
            }
            else
            {
                sendErrorResponse(static_cast<uint8_t>(SysExCommand::GET_ALL_PEERS), SysExErrorCode::NOT_READY);
            }
        }

        void handleGetPeer(const uint8_t *payload, uint16_t length)
        {
            if (!_onGetPeer)
            {
                EspNowMidiLog::w("SysEx: No GET_PEER handler");
                sendErrorResponse(static_cast<uint8_t>(SysExCommand::GET_PEER), SysExErrorCode::NOT_READY);
                return;
            }

            uint8_t index;
            if (SysExDecoder::decodeIndex(payload, length, index))
            {
                EspNowMidiLog::d("SysEx: Getting peer at index %u", index);
                _onGetPeer(index);
            }
            else
            {
                EspNowMidiLog::w("SysEx: Failed to decode peer index");
                sendErrorResponse(static_cast<uint8_t>(SysExCommand::GET_PEER), SysExErrorCode::DECODE_FAILED);
            }
        }

        void handleGetConfig()
        {
            if (_onGetConfig)
            {
                EspNowMidiLog::d("SysEx: Getting full board config");
                _onGetConfig();
            }
            else
            {
                sendErrorResponse(static_cast<uint8_t>(SysExCommand::GET_CONFIG), SysExErrorCode::NOT_READY);
            }
        }

        void handleSetMidiLoopback(const uint8_t *payload, uint16_t length)
        {
            if (!_onSetMidiLoopback)
            {
                EspNowMidiLog::w("SysEx: No SET_MIDI_LOOPBACK handler");
                sendErrorResponse(static_cast<uint8_t>(SysExCommand::SET_MIDI_LOOPBACK), SysExErrorCode::NOT_READY);
                return;
            }

            bool enabled;
            if (SysExDecoder::decodeMidiLoopback(payload, length, enabled))
            {
                EspNowMidiLog::d("SysEx: Setting MIDI loopback %s", enabled ? "on" : "off");
                _onSetMidiLoopback(enabled);
            }
            else
            {
                EspNowMidiLog::w("SysEx: Failed to decode MIDI loopback flag");
                sendErrorResponse(static_cast<uint8_t>(SysExCommand::SET_MIDI_LOOPBACK), SysExErrorCode::DECODE_FAILED);
            }
        }

        void handleGetMidiLoopback()
        {
            if (_onGetMidiLoopback)
            {
                EspNowMidiLog::d("SysEx: Getting MIDI loopback");
                _onGetMidiLoopback();
            }
            else
            {
                sendErrorResponse(static_cast<uint8_t>(SysExCommand::GET_MIDI_LOOPBACK), SysExErrorCode::NOT_READY);
            }
        }

        void handleSetPowerSave(const uint8_t *payload, uint16_t length)
        {
            if (!_onSetPowerSave)
            {
                EspNowMidiLog::w("SysEx: No SET_POWER_SAVE handler");
                sendErrorResponse(static_cast<uint8_t>(SysExCommand::SET_POWER_SAVE), SysExErrorCode::NOT_READY);
                return;
            }

            bool enabled;
            if (SysExDecoder::decodePowerSave(payload, length, enabled))
            {
                EspNowMidiLog::d("SysEx: Setting power save %s", enabled ? "on" : "off");
                _onSetPowerSave(enabled);
            }
            else
            {
                EspNowMidiLog::w("SysEx: Failed to decode power save flag");
                sendErrorResponse(static_cast<uint8_t>(SysExCommand::SET_POWER_SAVE), SysExErrorCode::DECODE_FAILED);
            }
        }

        void handleGetPowerSave()
        {
            if (_onGetPowerSave)
            {
                EspNowMidiLog::d("SysEx: Getting power save");
                _onGetPowerSave();
            }
            else
            {
                sendErrorResponse(static_cast<uint8_t>(SysExCommand::GET_POWER_SAVE), SysExErrorCode::NOT_READY);
            }
        }

        void handleReset()
        {
            if (_onReset)
            {
                EspNowMidiLog::i("SysEx: Performing reset");
                _onReset();
            }
            else
            {
                sendErrorResponse(static_cast<uint8_t>(SysExCommand::RESET), SysExErrorCode::NOT_READY);
            }
        }

        void handleGetVersion()
        {
            if (_onGetVersion)
            {
                EspNowMidiLog::d("SysEx: Getting version");
                _onGetVersion();
            }
            else
            {
                sendErrorResponse(static_cast<uint8_t>(SysExCommand::GET_VERSION), SysExErrorCode::NOT_READY);
            }
        }
    };
}
