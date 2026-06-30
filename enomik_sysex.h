#pragma once

#include "./enomik_sysex_codec.h"

#ifdef ARDUINO
#include <Arduino.h>
#endif

#include <functional>

namespace enomik
{
    class SysExHandler
    {
    public:
        // Callbacks for different SysEx commands
        using PinConfigCallback = std::function<void(const PinConfig &)>;
        using PinQueryCallback = std::function<void(uint8_t pin)>;
        using PeerQueryCallback = std::function<void(uint8_t index)>;
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
        void setOnGetAllPeers(VoidCallback cb) { _onGetAllPeers = cb; }
        void setOnGetPeer(PeerQueryCallback cb) { _onGetPeer = cb; }
        void setOnGetConfig(VoidCallback cb) { _onGetConfig = cb; }
        void setOnReset(VoidCallback cb) { _onReset = cb; }
        void setOnGetVersion(VoidCallback cb) { _onGetVersion = cb; }
        void setOnSend(SendCallback cb) { _onSend = cb; }

        // Main entry point for handling incoming SysEx messages
        void handleSysEx(const uint8_t *data, uint16_t length)
        {
            // Validate minimum length
            if (length < SysExPacket::MIN_PACKET_SIZE)
            {
                Serial.println("SysEx: Invalid packet length");
                sendErrorResponse(0, SysExErrorCode::DECODE_FAILED);
                return;
            }

            // Create packet wrapper
            SysExPacket packet;
            packet.length = length;
            memcpy(packet.data, data, length);

            if (!packet.isValid())
            {
                Serial.println("SysEx: Invalid packet format");
                sendErrorResponse(0, SysExErrorCode::DECODE_FAILED);
                return;
            }

            // Check version compatibility
            if (!packet.isVersionCompatible())
            {
                Serial.print("SysEx: Incompatible protocol version ");
                Serial.print(packet.getMajorVersion());
                Serial.print(".");
                Serial.print(packet.getMinorVersion());
                Serial.print(" (expected ");
                Serial.print(PROTOCOL_VERSION_MAJOR);
                Serial.print(".x)");
                Serial.println();
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

    private:
        PinConfigCallback _onSetPinConfig;
        PinQueryCallback _onGetPinConfig;
        PinQueryCallback _onDeletePinConfig;
        VoidCallback _onClearPinConfigs;
        VoidCallback _onGetAllPinConfigs;
        VoidCallback _onGetMAC;
        MACCallback _onAddPeer;
        VoidCallback _onGetAllPeers;
        PeerQueryCallback _onGetPeer;
        VoidCallback _onGetConfig;
        VoidCallback _onReset;
        VoidCallback _onGetVersion;
        SendCallback _onSend;

        void routeCommand(const SysExPacket &packet)
        {
            SysExCommand cmd = packet.getCommand();
            const uint8_t *payload = packet.getPayload();
            uint16_t payloadLen = packet.getPayloadLength();

            Serial.print("SysEx: Handling command 0x");
            Serial.println(static_cast<uint8_t>(cmd), HEX);

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

            case SysExCommand::RESET:
                handleReset();
                break;

            case SysExCommand::GET_VERSION:
                handleGetVersion();
                break;

            default:
                Serial.print("SysEx: Unknown command: 0x");
                Serial.println(static_cast<uint8_t>(cmd), HEX);
                sendErrorResponse(static_cast<uint8_t>(cmd), SysExErrorCode::UNKNOWN_COMMAND);
                break;
            }
        }

        void handleSetPinConfig(const uint8_t *payload, uint16_t length)
        {
            if (!_onSetPinConfig)
            {
                Serial.println("SysEx: No SET_PIN_CONFIG handler");
                sendErrorResponse(static_cast<uint8_t>(SysExCommand::SET_PIN_CONFIG), SysExErrorCode::NOT_READY);
                return;
            }

            PinConfig cfg(0, 0);
            if (SysExDecoder::decodePinConfig(payload, length, cfg))
            {
                Serial.print("SysEx: Setting config for pin ");
                Serial.println(cfg.pin);
                _onSetPinConfig(cfg);
            }
            else
            {
                Serial.println("SysEx: Failed to decode pin config");
                sendErrorResponse(static_cast<uint8_t>(SysExCommand::SET_PIN_CONFIG), SysExErrorCode::DECODE_FAILED);
            }
        }

        void handleGetPinConfig(const uint8_t *payload, uint16_t length)
        {
            if (!_onGetPinConfig)
            {
                Serial.println("SysEx: No GET_PIN_CONFIG handler");
                sendErrorResponse(static_cast<uint8_t>(SysExCommand::GET_PIN_CONFIG), SysExErrorCode::NOT_READY);
                return;
            }

            uint8_t pin;
            if (SysExDecoder::decodePin(payload, length, pin))
            {
                Serial.print("SysEx: Getting config for pin ");
                Serial.println(pin);
                _onGetPinConfig(pin);
            }
            else
            {
                Serial.println("SysEx: Failed to decode pin number");
                sendErrorResponse(static_cast<uint8_t>(SysExCommand::GET_PIN_CONFIG), SysExErrorCode::DECODE_FAILED);
            }
        }

        void handleDeletePinConfig(const uint8_t *payload, uint16_t length)
        {
            if (!_onDeletePinConfig)
            {
                Serial.println("SysEx: No DELETE_PIN_CONFIG handler");
                sendErrorResponse(static_cast<uint8_t>(SysExCommand::DELETE_PIN_CONFIG), SysExErrorCode::NOT_READY);
                return;
            }

            uint8_t pin;
            if (SysExDecoder::decodePin(payload, length, pin))
            {
                Serial.print("SysEx: Deleting config for pin ");
                Serial.println(pin);
                _onDeletePinConfig(pin);
            }
            else
            {
                Serial.println("SysEx: Failed to decode pin number");
                sendErrorResponse(static_cast<uint8_t>(SysExCommand::DELETE_PIN_CONFIG), SysExErrorCode::DECODE_FAILED);
            }
        }

        void handleClearPinConfigs()
        {
            if (_onClearPinConfigs)
            {
                Serial.println("SysEx: Clearing all pin configs");
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
                Serial.println("SysEx: Getting all pin configs");
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
                Serial.println("SysEx: Getting MAC address");
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
                Serial.println("SysEx: No ADD_PEER handler");
                sendErrorResponse(static_cast<uint8_t>(SysExCommand::ADD_PEER), SysExErrorCode::NOT_READY);
                return;
            }

            uint8_t mac[6];
            if (SysExDecoder::decodeMAC(payload, length, mac))
            {
                Serial.print("SysEx: Adding peer: ");
                for (int i = 0; i < 6; i++)
                {
                    Serial.print(mac[i], HEX);
                    if (i < 5)
                        Serial.print(":");
                }
                Serial.println();
                _onAddPeer(mac);
            }
            else
            {
                Serial.println("SysEx: Failed to decode MAC address");
                sendErrorResponse(static_cast<uint8_t>(SysExCommand::ADD_PEER), SysExErrorCode::DECODE_FAILED);
            }
        }

        void handleGetAllPeers()
        {
            if (_onGetAllPeers)
            {
                Serial.println("SysEx: Getting all peers");
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
                Serial.println("SysEx: No GET_PEER handler");
                sendErrorResponse(static_cast<uint8_t>(SysExCommand::GET_PEER), SysExErrorCode::NOT_READY);
                return;
            }

            uint8_t index;
            if (SysExDecoder::decodeIndex(payload, length, index))
            {
                Serial.print("SysEx: Getting peer at index ");
                Serial.println(index);
                _onGetPeer(index);
            }
            else
            {
                Serial.println("SysEx: Failed to decode peer index");
                sendErrorResponse(static_cast<uint8_t>(SysExCommand::GET_PEER), SysExErrorCode::DECODE_FAILED);
            }
        }

        void handleGetConfig()
        {
            if (_onGetConfig)
            {
                Serial.println("SysEx: Getting full board config");
                _onGetConfig();
            }
            else
            {
                sendErrorResponse(static_cast<uint8_t>(SysExCommand::GET_CONFIG), SysExErrorCode::NOT_READY);
            }
        }

        void handleReset()
        {
            if (_onReset)
            {
                Serial.println("SysEx: Performing reset");
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
                Serial.println("SysEx: Getting version");
                _onGetVersion();
            }
            else
            {
                sendErrorResponse(static_cast<uint8_t>(SysExCommand::GET_VERSION), SysExErrorCode::NOT_READY);
            }
        }
    };
}
