#include "./config.h"
#include "esp_now_midi.h"
#include <esp_now.h>
#include <WiFi.h>
#include "enomik_io.h"
#include "PeerStorage.h"
#include "utils/esp.h"
#include "utils/mac.h"

#ifdef HAS_USB_MIDI
#include <Adafruit_TinyUSB.h>
#include <MIDI.h>

// Global USB MIDI objects - MUST be at file scope
Adafruit_USBD_MIDI g_usb_midi;
MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, g_usb_midi, USBMIDI);
#endif

namespace enomik
{
    class Client
    {
    private:
        PeerStorage peerStorage;
        bool isInitialized;

        // --- Channel Voice ---
        std::function<void(byte channel, byte note, byte velocity)> _onNoteOnHandler;
        std::function<void(byte channel, byte note, byte velocity)> _onNoteOffHandler;
        std::function<void(byte channel, byte control, byte value)> _onControlChangeHandler;
        std::function<void(byte channel, byte program)> _onProgramChangeHandler;
        std::function<void(byte channel, byte pressure)> _onAfterTouchChannelHandler;         // Channel aftertouch
        std::function<void(byte channel, byte note, byte pressure)> _onAfterTouchPolyHandler; // Poly aftertouch
        std::function<void(byte channel, int value)> _onPitchBendHandler;                     // uint16_t

        // --- System Real-Time ---
        std::function<void()> _onStartHandler;
        std::function<void()> _onStopHandler;
        std::function<void()> _onContinueHandler;
        std::function<void()> _onClockHandler;

        // --- System Common ---
        std::function<void(uint16_t songPosition)> _onSongPositionHandler;
        std::function<void(byte songNumber)> _onSongSelectHandler;

        // --- System Exclusive ---
        std::function<void(uint8_t *data, unsigned int length)> _onSysExHandler;

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 3, 0)
        static void onDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status)
        {
        }
#else
        static void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status)
        {
        }
#endif

        void onSystemExclusive(uint8_t *data, unsigned int length)
        {
            io.onSysEx(data, length);
            if (_onSysExHandler)
            {
                _onSysExHandler(data, length);
            }
        }

        // --- Static handlers that call both IO and user-defined callbacks ---
        static void handleNoteOnStatic(byte channel, byte note, byte velocity)
        {
            if (Client::instancePtr)
            {
                Client::instancePtr->io.onNoteOn(channel, note, velocity);
                if (Client::instancePtr->_onNoteOnHandler)
                {
                    Client::instancePtr->_onNoteOnHandler(channel, note, velocity);
                }
            }
        }

        static void handleNoteOffStatic(byte channel, byte note, byte velocity)
        {
            if (Client::instancePtr)
            {
                Client::instancePtr->io.onNoteOff(channel, note, velocity);
                if (Client::instancePtr->_onNoteOffHandler)
                    Client::instancePtr->_onNoteOffHandler(channel, note, velocity);
            }
        }

        static void handleControlChangeStatic(byte channel, byte control, byte value)
        {
            if (Client::instancePtr)
            {
                Client::instancePtr->io.onControlChange(channel, control, value);
                if (Client::instancePtr->_onControlChangeHandler)
                    Client::instancePtr->_onControlChangeHandler(channel, control, value);
            }
        }

        static void handleProgramChangeStatic(byte channel, byte program)
        {
            if (Client::instancePtr)
            {
                Client::instancePtr->io.onProgramChange(channel, program);
                if (Client::instancePtr->_onProgramChangeHandler)
                    Client::instancePtr->_onProgramChangeHandler(channel, program);
            }
        }

        static void handleAfterTouchChannelStatic(byte channel, byte pressure)
        {
            if (Client::instancePtr)
            {
                // Client::instancePtr->io.onAfterTouch(channel, pressure);
                if (Client::instancePtr->_onAfterTouchChannelHandler)
                    Client::instancePtr->_onAfterTouchChannelHandler(channel, pressure);
            }
        }

        static void handleAfterTouchPolyStatic(byte channel, byte note, byte pressure)
        {
            if (Client::instancePtr)
            {
                // Client::instancePtr->io.onAfterTouchPoly(channel, note, pressure);
                if (Client::instancePtr->_onAfterTouchPolyHandler)
                    Client::instancePtr->_onAfterTouchPolyHandler(channel, note, pressure);
            }
        }

        static void handlePitchBendStatic(byte channel, int value) // uint16_t
        {
            if (Client::instancePtr)
            {
                Client::instancePtr->io.onPitchBend(channel, value);
                if (Client::instancePtr->_onPitchBendHandler)
                    Client::instancePtr->_onPitchBendHandler(channel, value);
            }
        }

        // --- System Real-Time ---
        static void handleStartStatic()
        {
            if (Client::instancePtr && Client::instancePtr->_onStartHandler)
                Client::instancePtr->_onStartHandler();
        }

        static void handleStopStatic()
        {
            if (Client::instancePtr && Client::instancePtr->_onStopHandler)
                Client::instancePtr->_onStopHandler();
        }

        static void handleContinueStatic()
        {
            if (Client::instancePtr && Client::instancePtr->_onContinueHandler)
                Client::instancePtr->_onContinueHandler();
        }

        static void handleClockStatic()
        {
            if (Client::instancePtr && Client::instancePtr->_onClockHandler)
                Client::instancePtr->_onClockHandler();
        }

        // --- System Common ---
        static void handleSongPositionStatic(uint16_t songPosition)
        {
            if (Client::instancePtr && Client::instancePtr->_onSongPositionHandler)
                Client::instancePtr->_onSongPositionHandler(songPosition);
        }

        static void handleSongSelectStatic(byte songNumber)
        {
            if (Client::instancePtr && Client::instancePtr->_onSongSelectHandler)
                Client::instancePtr->_onSongSelectHandler(songNumber);
        }

        // --- System Exclusive ---
        static void handleSysExStatic(uint8_t *data, unsigned int length)
        {
            if (Client::instancePtr)
            {
                Client::instancePtr->onSystemExclusive(data, length);
                if (Client::instancePtr->_onSysExHandler)
                    Client::instancePtr->_onSysExHandler(data, length);
            }
        }

    public:
        static Client *instancePtr;
        esp_now_midi espnowMIDI;
        enomik::IO io;

        Client() : isInitialized(false)
        {
            instancePtr = this;
        }

        void begin()
        {
            io.begin();
            io.setOnMIDISendRequest([this](midi_message msg)
                                    {
                                //send ESP-NOW MIDI
                                // this->espnowMIDI.sendToAllPeers((uint8_t *)&msg, sizeof(msg));

                                switch(msg.status) {
                                    case MIDI_NOTE_ON:
                                        sendNoteOn(msg.firstByte, msg.secondByte, msg.channel);
                                        break;
                                    case MIDI_NOTE_OFF:
                                        sendNoteOff(msg.firstByte, msg.secondByte, msg.channel);
                                        break;
                                    case MIDI_CONTROL_CHANGE:
                                        sendControlChange(msg.firstByte, msg.secondByte, msg.channel);
                                        break;
                                    case MIDI_PROGRAM_CHANGE:
                                        sendProgramChange(msg.firstByte, msg.channel);
                                        break;
                                    case MIDI_PITCH_BEND:
                                    {
                                        int value = (msg.secondByte << 7) | msg.firstByte;
                                        sendPitchBend(value, msg.channel);
                                        break;
                                    }
                                    case MIDI_AFTERTOUCH:
                                    {
                                        sendAfterTouch(msg.firstByte, msg.channel);
                                        break;
                                    }
                                    case MIDI_POLY_AFTERTOUCH:
                                    {
                                        sendPolyAfterTouch(msg.firstByte, msg.secondByte, msg.channel);
                                        break;
                                    }
                                    case MIDI_START:
                                        sendStart();
                                        break;
                                    case MIDI_STOP:
                                        sendStop();
                                        break;
                                    case MIDI_CONTINUE:
                                        sendContinue();
                                        break;
                                    case MIDI_TIME_CLOCK:
                                        sendClock();
                                        break;
                                    case MIDI_TIME_CODE:
                                        break;
                                    case MIDI_SONG_POS_POINTER:
                                    {
                                        uint16_t songPos = (msg.secondByte << 7) | msg.firstByte;
                                        sendSongPosition(songPos);
                                        break;
                                    }
                                    case MIDI_SONG_SELECT:
                                        sendSongSelect(msg.firstByte);
                                        break;      
                                    default:
                                        Serial.println("Sent other MIDI message");
                                } });

            // TODO: this should be part of the other handler
            io.setOnSysExSendRequest([this](midi_sysex_message msg)
                                     { this->sendSysEx(msg.data, msg.length); });

            io.setOnAddPeerRequest([this](uint8_t mac[])
                                   {
                               Serial.println("IO requested to add peer:");
                               macPrint(mac);
                               if (this->espnowMIDI.addPeer(mac))
                               {
                                   // Store peer in persistent storage
                                   if (this->peerStorage.add(mac))
                                   {
                                       Serial.println("Peer added and stored successfully");
                                       return true;
                                   }
                                   else
                                   {
                                       Serial.println("Failed to store peer");
                                       return false;
                                   }
                               }
                               else
                               {
                                   Serial.println("Failed to add peer to ESP-NOW");
                                   return false;
                               } });

            io.setOnGetPeersRequest([this]()
                                    {
        Serial.println("GET_PEERS request received");
        
        // Print peers
        for (int i = 0; i < this->peerStorage.count(); i++)
        {
            const uint8_t *mac = this->peerStorage.get(i);
            if (mac)
            {
                Serial.print("Peer ");
                Serial.print(i);
                Serial.print(": ");
                macPrint(mac);
                Serial.println();
            }
        } 
        
        midi_sysex_message msg;
        msg.data[0] = 0xF0;
        msg.data[1] = 0x7D; // Manufacturer ID (non-commercial)
        msg.data[2] = static_cast<uint8_t>(SysExCommand::GET_PEERS_RESPONSE);
        auto index = 3;
        
        for (int i = 0; i < this->peerStorage.count(); i++)
        {
            const uint8_t *mac = this->peerStorage.get(i);
            if (mac)
            {
                // Encode MAC address (6 bytes -> 12 bytes in 7-bit format)
                for (int j = 0; j < 6; j++)
                {
                    msg.data[index++] = (mac[j] >> 4) & 0x0F;  // High nibble
                    msg.data[index++] = mac[j] & 0x0F;         // Low nibble
                }
            }
        }
        
        msg.data[index] = 0xF7;
        msg.length = index + 1;
        Serial.println("Sending peer list via SysEx");
        Serial.println("Total peers: " + String(this->peerStorage.count()));
        
        this->sendSysEx(msg.data, msg.length); });

            io.setOnResetRequest([this]()
                                 {
                                     this->peerStorage.clear();
                                     this->espnowMIDI.clearPeers();

                                     //  esp_now_deinit();
                                     //  delay(10);
                                     //  esp_now_init();
                                 });

#ifdef HAS_USB_MIDI
            // Initialize USB MIDI using global instance
            USBMIDI.begin(MIDI_CHANNEL_OMNI);
            USBMIDI.turnThruOff();

            // Set USB descriptors
            TinyUSBDevice.setManufacturerDescriptor("grantler instruments");
            TinyUSBDevice.setProductDescriptor("enomik3000_client");

            // If already enumerated, re-enumerate
            if (TinyUSBDevice.mounted())
            {
                TinyUSBDevice.detach();
                delay(10);
                TinyUSBDevice.attach();
            }

            Serial.println("USB MIDI initialized");
#endif

            // Initialize ESP-NOW MIDI
            espnowMIDI.begin();

            // --- Set handlers for ESP-NOW ---
            espnowMIDI.setHandleNoteOn(handleNoteOnStatic);
            espnowMIDI.setHandleNoteOff(handleNoteOffStatic);
            espnowMIDI.setHandleControlChange(handleControlChangeStatic);
            espnowMIDI.setHandleProgramChange(handleProgramChangeStatic);
            espnowMIDI.setHandleAfterTouchChannel(handleAfterTouchChannelStatic);
            espnowMIDI.setHandleAfterTouchPoly(handleAfterTouchPolyStatic);
            espnowMIDI.setHandlePitchBend(handlePitchBendStatic);
            espnowMIDI.setHandleStart(handleStartStatic);
            espnowMIDI.setHandleStop(handleStopStatic);
            espnowMIDI.setHandleContinue(handleContinueStatic);
            espnowMIDI.setHandleClock(handleClockStatic);
            espnowMIDI.setHandleSongPosition(handleSongPositionStatic);
            espnowMIDI.setHandleSongSelect(handleSongSelectStatic);

#ifdef HAS_USB_MIDI
            // --- Set handlers for USB MIDI ---
            USBMIDI.setHandleSystemExclusive(handleSysExStatic);
            USBMIDI.setHandleNoteOn(handleNoteOnStatic);
            USBMIDI.setHandleNoteOff(handleNoteOffStatic);
            USBMIDI.setHandleControlChange(handleControlChangeStatic);
            USBMIDI.setHandleProgramChange(handleProgramChangeStatic);
            USBMIDI.setHandleAfterTouchChannel(handleAfterTouchChannelStatic);
            USBMIDI.setHandleAfterTouchPoly(handleAfterTouchPolyStatic);
            USBMIDI.setHandlePitchBend(handlePitchBendStatic);
            USBMIDI.setHandleStart(handleStartStatic);
            USBMIDI.setHandleStop(handleStopStatic);
            USBMIDI.setHandleContinue(handleContinueStatic);
            USBMIDI.setHandleClock(handleClockStatic);
            USBMIDI.setHandleSongSelect(handleSongSelectStatic);
#endif

            // Initialize peer storage (handles EEPROM internally)
            if (!peerStorage.begin())
            {
                Serial.println("Failed to initialize peer storage");
                return;
            }

            Serial.println("Restoring peers from storage...");
            int restoredCount = 0;
            int skippedCount = 0;

            // Restore all peers from storage to ESP-NOW
            for (int i = 0; i < peerStorage.count(); i++)
            {
                const uint8_t *mac = peerStorage.get(i);
                if (mac)
                {
                    // Check if peer already exists before adding
                    if (!espnowMIDI.hasPeer(mac))
                    {
                        if (espnowMIDI.addPeer(mac))
                        {
                            Serial.print("Restored peer: ");
                            Serial.println(macToString(mac));
                            restoredCount++;
                        }
                        else
                        {
                            Serial.print("Failed to restore peer: ");
                            Serial.println(macToString(mac));
                        }
                    }
                    else
                    {
                        Serial.print("Peer already exists, skipping: ");
                        Serial.println(macToString(mac));
                        skippedCount++;
                    }
                }
            }

            Serial.printf("Peer restoration complete: %d restored, %d skipped\n",
                          restoredCount, skippedCount);

            isInitialized = true;
            sendHandShake();
        }

        void loop()
        {
#ifdef HAS_USB_MIDI
            USBMIDI.read();
#endif
            io.loop();
        }

        bool sendNoteOn(byte note, byte velocity, byte channel)
        {
            auto err = espnowMIDI.sendNoteOn(note, velocity, channel);
#ifdef HAS_USB_MIDI
            if (TinyUSBDevice.mounted() && TinyUSBDevice.ready())
            {

                USBMIDI.sendNoteOn(note, velocity, channel);
            }
#endif
            if (err != ESP_OK)
            {
                return false; // ESP-NOW failed
            }
            return true;
        }

        bool sendNoteOff(byte note, byte velocity, byte channel)
        {
            auto err = espnowMIDI.sendNoteOff(note, velocity, channel);
#ifdef HAS_USB_MIDI
            if (TinyUSBDevice.mounted() && TinyUSBDevice.ready())
            {
                USBMIDI.sendNoteOff(note, velocity, channel);
            }
#endif
            if (err != ESP_OK)
            {
                return false; // ESP-NOW failed
            }
            return true;
        }

        bool sendControlChange(byte control, byte value, byte channel)
        {
            auto err = espnowMIDI.sendControlChange(control, value, channel);
#ifdef HAS_USB_MIDI
            if (TinyUSBDevice.mounted() && TinyUSBDevice.ready())
            {
                USBMIDI.sendControlChange(control, value, channel);
            }
#endif
            if (err != ESP_OK)
            {
                return false; // ESP-NOW failed
            }
            return true;
        }

        bool sendProgramChange(byte program, byte channel)
        {
            auto err = espnowMIDI.sendProgramChange(program, channel);
#ifdef HAS_USB_MIDI
            if (TinyUSBDevice.mounted() && TinyUSBDevice.ready())
            {
                USBMIDI.sendProgramChange(program, channel);
            }
#endif
            if (err != ESP_OK)
            {
                return false; // ESP-NOW failed
            }
            return true;
        }

        bool sendAfterTouch(byte pressure, byte channel)
        {
            auto err = espnowMIDI.sendAfterTouch(pressure, channel);
#ifdef HAS_USB_MIDI
            if (TinyUSBDevice.mounted() && TinyUSBDevice.ready())
            {
                USBMIDI.sendAfterTouch(pressure, channel);
            }
#endif
            if (err != ESP_OK)
            {
                return false; // ESP-NOW failed
            }
            return true;
        }

        bool sendPolyAfterTouch(byte note, byte pressure, byte channel)
        {
            auto err = espnowMIDI.sendAfterTouchPoly(note, pressure, channel);
#ifdef HAS_USB_MIDI
            if (TinyUSBDevice.mounted() && TinyUSBDevice.ready())
            {
                USBMIDI.sendAfterTouch(note, pressure, channel);
            }
#endif
            if (err != ESP_OK)
            {
                return false; // ESP-NOW failed
            }
            return true;
        }

        bool sendPitchBend(int value, byte channel) // uint16_t
        {
            auto err = espnowMIDI.sendPitchBend(value, channel);
#ifdef HAS_USB_MIDI
            if (TinyUSBDevice.mounted() && TinyUSBDevice.ready())
            {
                USBMIDI.sendPitchBend(value, channel);
            }
#endif
            if (err != ESP_OK)
            {
                return false; // ESP-NOW failed
            }
            return true;
        }

        bool sendStart()
        {
            auto err = espnowMIDI.sendStart();
#ifdef HAS_USB_MIDI
            if (TinyUSBDevice.mounted() && TinyUSBDevice.ready())
            {
                USBMIDI.sendStart();
            }
#endif
            if (err != ESP_OK)
            {
                return false; // ESP-NOW failed
            }
            return true;
        }

        bool sendStop()
        {
            auto err = espnowMIDI.sendStop();
#ifdef HAS_USB_MIDI
            if (TinyUSBDevice.mounted() && TinyUSBDevice.ready())
            {
                USBMIDI.sendStop();
            }
#endif
            if (err != ESP_OK)
            {
                return false; // ESP-NOW failed
            }
            return true;
        }

        bool sendContinue()
        {
            auto err = espnowMIDI.sendContinue();
#ifdef HAS_USB_MIDI
            if (TinyUSBDevice.mounted() && TinyUSBDevice.ready())
            {
                USBMIDI.sendContinue();
            }
#endif
            if (err != ESP_OK)
            {
                return false; // ESP-NOW failed
            }
            return true;
        }

        bool sendClock()
        {
            auto err = espnowMIDI.sendClock();
#ifdef HAS_USB_MIDI
            if (TinyUSBDevice.mounted() && TinyUSBDevice.ready())
            {
                USBMIDI.sendClock();
            }
#endif
            if (err != ESP_OK)
            {
                return false; // ESP-NOW failed
            }
            return true;
        }

        bool sendSongPosition(uint16_t value)
        {
            auto err = espnowMIDI.sendSongPosition(value);
#ifdef HAS_USB_MIDI
            if (TinyUSBDevice.mounted() && TinyUSBDevice.ready())
            {
                USBMIDI.sendSongPosition(value);
            }
#endif
            if (err != ESP_OK)
            {
                return false; // ESP-NOW failed
            }
            return true;
        }

        bool sendSongSelect(uint8_t value)
        {
            auto err = espnowMIDI.sendSongSelect(value);
#ifdef HAS_USB_MIDI
            if (TinyUSBDevice.mounted() && TinyUSBDevice.ready())
            {
                USBMIDI.sendSongSelect(value);
            }
#endif
            if (err != ESP_OK)
            {
                return false; // ESP-NOW failed
            }
            return true;
        }

        bool sendSysEx(const uint8_t *data, uint16_t length)
        {
            auto err = espnowMIDI.sendSysex((uint8_t *)data, length);
#ifdef HAS_USB_MIDI
            if (TinyUSBDevice.mounted() && TinyUSBDevice.ready())
            {
                USBMIDI.sendSysEx(length, data);
            }
#endif
            if (err != ESP_OK)
            {
                return false; // ESP-NOW failed
            }
            return true;
        }
        // --- Channel Voice ---
        void setHandleNoteOn(std::function<void(byte channel, byte note, byte velocity)> handler)
        {
            _onNoteOnHandler = handler;
        }

        void setHandleNoteOff(std::function<void(byte channel, byte note, byte velocity)> handler)
        {
            _onNoteOffHandler = handler;
        }

        void setHandleControlChange(std::function<void(byte channel, byte control, byte value)> handler)
        {
            _onControlChangeHandler = handler;
        }

        void setHandleProgramChange(std::function<void(byte channel, byte program)> handler)
        {
            _onProgramChangeHandler = handler;
        }

        void setHandleAfterTouchChannel(std::function<void(byte channel, byte pressure)> handler)
        {
            _onAfterTouchChannelHandler = handler;
        }

        void setHandleAfterTouchPoly(std::function<void(byte channel, byte note, byte pressure)> handler)
        {
            _onAfterTouchPolyHandler = handler;
        }

        void setHandlePitchBend(std::function<void(byte channel, int value)> handler) // uint16_t
        {
            _onPitchBendHandler = handler;
        }

        // --- System Real-Time ---
        void setHandleStart(std::function<void()> handler)
        {
            _onStartHandler = handler;
        }

        void setHandleStop(std::function<void()> handler)
        {
            _onStopHandler = handler;
        }

        void setHandleContinue(std::function<void()> handler)
        {
            _onContinueHandler = handler;
        }

        void setHandleClock(std::function<void()> handler)
        {
            _onClockHandler = handler;
        }

        // --- System Common ---
        void setHandleSongPosition(std::function<void(uint16_t songPosition)> handler)
        {
            _onSongPositionHandler = handler;
        }

        void setHandleSongSelect(std::function<void(byte songNumber)> handler)
        {
            _onSongSelectHandler = handler;
        }

        // --- System Exclusive ---
        void setHandleSysEx(std::function<void(uint8_t *data, unsigned int length)> handler)
        {
            _onSysExHandler = handler;
        }

        void sendHandShake()
        {
            if (!isInitialized)
            {
                Serial.println("Client not initialized. Cannot send handshake.");
                return;
            }
            // TODO: refactor to use sysex instead of control change
            // also add a heartbeat mechanism

            byte channel = 16;
            byte control = 127;
            byte value = 127;

            for (int i = 0; i < peerStorage.count(); i++)
            {
                const uint8_t *mac = peerStorage.get(i);
                if (mac)
                {
                    bool ok = espnowMIDI.sendControlChange(control, value, channel);
                    if (ok)
                    {
                    }
                    else
                    {
                        Serial.print("Failed to send handshake to: ");
                        Serial.println(macToString(mac));
                    }
                }
            }
        }

        // Simplified peer management - delegates to PeerStorage
        bool addPeer(const uint8_t mac[6])
        {
            if (!isInitialized)
            {
                Serial.println("Client not initialized. Call begin() first.");
                return false;
            }

            // Add to storage first
            if (!peerStorage.add(mac))
            {
                return false;
            }

            // Then add to ESP-NOW
            if (!espnowMIDI.addPeer(mac))
            {
                Serial.println("Failed to add peer to ESP-NOW");
                // Rollback storage change
                peerStorage.remove(mac);
                return false;
            }

            return true;
        }

        bool addPeerFromString(const String &macStr)
        {
            uint8_t mac[6];
            if (!macFromString(macStr, mac))
            {
                return false;
            }
            return addPeer(mac);
        }

        bool removePeer(const uint8_t *mac)
        {
            if (peerStorage.remove(mac))
            {
                // Also remove from ESP-NOW if needed
                // midi.removePeer(mac);  // if your midi class supports this
                return true;
            }
            return false;
        }

        bool removePeer(int index)
        {
            const uint8_t *mac = peerStorage.get(index);
            if (mac)
            {
                return removePeer(mac);
            }
            return false;
        }

        void clearAllPeers()
        {
            peerStorage.clear();
            // Also clear ESP-NOW peers if needed
        }

        void listPeers()
        {
            peerStorage.printAll();
        }

        int getPeerCount()
        {
            return peerStorage.count();
        }

        const uint8_t *getPeer(int index)
        {
            return peerStorage.get(index);
        }

        String getMacString(int index)
        {
            const uint8_t *mac = peerStorage.get(index);
            if (!mac)
            {
                return "";
            }
            return macToString(mac);
        }
    };

    Client *Client::instancePtr = nullptr;
};