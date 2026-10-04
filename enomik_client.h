#pragma once

#include "./config.h"
#include "esp_now_midi.h"
#include <esp_now.h>
#ifdef ARDUINO
#include <WiFi.h>
#endif
#include "include/enomik_io.h"
#include "include/PeerStorage.h"
#include "include/MidiInbox.h"
#include "include/UsbMidiPacket.h"
#include "include/esp_now_midi_compat.h"
#include "utils/esp.h"
#include "utils/mac.h"

#ifdef HAS_USB_MIDI
#ifdef ARDUINO
#include <Adafruit_TinyUSB.h>
#include <MIDI.h>

// Global USB MIDI objects - MUST be at file scope; distinct from Dongle symbols.
Adafruit_USBD_MIDI g_client_usb_midi;
MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, g_client_usb_midi, CLIENT_USBMIDI);
#elif defined(ESP_PLATFORM)
#include "include/esp_now_midi_usb.h"

// Global USB MIDI objects - MUST be at file scope; distinct from Dongle symbols.
TinyUsbRawMidiClass g_client_usb_midi;
TinyUsbMidiClass CLIENT_USBMIDI;
#endif
#endif

namespace enomik
{
    /**
     * @brief High-level MIDI client that bridges local I/O, ESP-NOW, and optional USB MIDI.
     *
     * MIDI channels use the user-facing 1–16 convention. Call begin() once
     * and call loop() regularly from the Arduino loop.
     */
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
        std::function<void(byte channel, int value)> _onPitchBendHandler; // signed; center = 0

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

        // Depth > 0 while dispatching a local loopback. Nested Client::send*
        // (e.g. client_echo handlers) still go over the wire but must not
        // re-enter loopback, or send → handler → send recurses forever.
        int _loopbackDepth = 0;
        MidiInbox _inbox;
        bool _dispatchInLoop = true;

        struct LoopbackScope
        {
            int &_depth;
            explicit LoopbackScope(int &depth) : _depth(depth) { ++_depth; }
            ~LoopbackScope() { --_depth; }
            LoopbackScope(const LoopbackScope &) = delete;
            LoopbackScope &operator=(const LoopbackScope &) = delete;
        };

        template <typename Fn>
        void maybeLoopback(Fn &&dispatch)
        {
            if (!io.isMidiLoopback() || _loopbackDepth > 0)
                return;
            LoopbackScope scope(_loopbackDepth);
            dispatch();
        }

        // USB MIDI goes out as whole packets: a busy host drops a message
        // cleanly instead of receiving part of it.
        static bool writeUsbPacket(const uint8_t packet[4])
        {
#ifdef HAS_USB_MIDI
            return TinyUSBDevice.mounted() && TinyUSBDevice.ready() && g_client_usb_midi.writePacket(packet);
#else
            (void)packet;
            return false;
#endif
        }

        static void sendToUsb(MidiStatus status, uint8_t channel = 0, uint8_t firstByte = 0, uint8_t secondByte = 0)
        {
            midi_message msg;
            msg.channel = channel;
            msg.status = status;
            msg.firstByte = firstByte;
            msg.secondByte = secondByte;
            uint8_t packet[4];
            if (toUsbMidiPacket(msg, packet))
            {
                writeUsbPacket(packet);
            }
        }

        static void sendPitchBendToUsb(int value, uint8_t channel)
        {
            const int raw = (value < -8192 ? -8192 : (value > 8191 ? 8191 : value)) + 8192;
            sendToUsb(MIDI_PITCH_BEND, channel, raw & 0x7F, raw >> 7);
        }

        static void sendSysExToUsb(const uint8_t *data, uint16_t length)
        {
            writeUsbMidiSysEx(data, length, writeUsbPacket);
        }

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

        static void handlePitchBendStatic(byte channel, int value)
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

        // --- ESP-NOW receive ---

        // Runs in the ESP-NOW receive callback (WiFi task).
        static void onEspNowMessageStatic(const uint8_t *mac, const midi_message &msg)
        {
            if (!Client::instancePtr)
                return;
            if (Client::instancePtr->_dispatchInLoop)
                Client::instancePtr->_inbox.push(msg, mac);
            else
                dispatch(msg);
        }

        static void dispatch(const midi_message &msg)
        {
            const int value14 = (msg.secondByte << 7) | msg.firstByte; // pitch bend, song position
            switch (msg.status)
            {
            case MIDI_NOTE_ON:
                handleNoteOnStatic(msg.channel, msg.firstByte, msg.secondByte);
                break;
            case MIDI_NOTE_OFF:
                handleNoteOffStatic(msg.channel, msg.firstByte, msg.secondByte);
                break;
            case MIDI_CONTROL_CHANGE:
                handleControlChangeStatic(msg.channel, msg.firstByte, msg.secondByte);
                break;
            case MIDI_PROGRAM_CHANGE:
                handleProgramChangeStatic(msg.channel, msg.firstByte);
                break;
            case MIDI_AFTERTOUCH:
                handleAfterTouchChannelStatic(msg.channel, msg.firstByte);
                break;
            case MIDI_POLY_AFTERTOUCH:
                handleAfterTouchPolyStatic(msg.channel, msg.firstByte, msg.secondByte);
                break;
            case MIDI_PITCH_BEND:
                handlePitchBendStatic(msg.channel, value14 - 8192);
                break;
            case MIDI_START:
                handleStartStatic();
                break;
            case MIDI_STOP:
                handleStopStatic();
                break;
            case MIDI_CONTINUE:
                handleContinueStatic();
                break;
            case MIDI_TIME_CLOCK:
                handleClockStatic();
                break;
            case MIDI_SONG_POS_POINTER:
                handleSongPositionStatic(static_cast<uint16_t>(value14));
                break;
            case MIDI_SONG_SELECT:
                handleSongSelectStatic(msg.firstByte);
                break;
            default:
                break;
            }
        }

        // --- System Exclusive ---
        static void handleSysExStatic(uint8_t *data, unsigned int length)
        {
            if (Client::instancePtr)
            {
                // onSystemExclusive() already forwards to io + _onSysExHandler
                Client::instancePtr->onSystemExclusive(data, length);
            }
        }

    public:
        static Client *instancePtr; ///< Active client used by static receive callbacks.
        esp_now_midi espnowMIDI;    ///< Underlying ESP-NOW MIDI transport.
        enomik::IO io;              ///< Local configurable I/O and SysEx interface.

        /** @brief Constructs the client and makes it the active callback instance. */
        Client() : isInitialized(false)
        {
            instancePtr = this;
            espnowMIDI.setHandleMessage(onEspNowMessageStatic);
        }

        /**
         * @brief Initializes I/O, ESP-NOW MIDI, optional USB MIDI, and stored peers.
         *
         * Restores peers from persistent storage and sends a handshake when
         * initialization succeeds. If peer storage or ESP-NOW cannot initialize,
         * returns `false` and the client remains unavailable for peer registration.
         * @return `true` when the client is ready.
         */
        bool begin()
        {
            io.begin();
            io.setOnPowerSaveChanged([this](bool enabled)
                                     { this->espnowMIDI.setReducePowerAtCostOfLatency(enabled); });
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
                                        EspNowMidiLog::d("Sent other MIDI message");
                                } });

            // Forward SysEx responses (including GET_ALL_PEERS) via the handler send path
            io.setOnSysExSendRequest([this](midi_sysex_message msg)
                                     { this->sendSysEx(msg.data, msg.length); });

            io.setOnAddPeerRequest([this](uint8_t mac[]) -> AddPeerResult
                                   {
                               EspNowMidiLog::i("IO requested to add peer:");
                               macPrint(mac);

                               if (!isInitialized)
                               {
                                   return AddPeerResult::OperationFailed;
                               }

                               if (peerStorage.isFull())
                               {
                                   EspNowMidiLog::w("Peer table full");
                                   return AddPeerResult::TableFull;
                               }

                               if (peerStorage.exists(mac))
                               {
                                   EspNowMidiLog::w("Peer already exists");
                                   return AddPeerResult::AlreadyExists;
                               }

                               if (!peerStorage.add(mac))
                               {
                                   EspNowMidiLog::e("Failed to store peer");
                                   return AddPeerResult::OperationFailed;
                               }

                               if (!espnowMIDI.addPeer(mac))
                               {
                                   EspNowMidiLog::e("Failed to add peer to ESP-NOW");
                                   peerStorage.remove(mac);
                                   return AddPeerResult::OperationFailed;
                               }

                               EspNowMidiLog::i("Peer added and stored successfully");
                               return AddPeerResult::Success;
                           });

            io.setOnGetPeerRequest([this](uint8_t index) -> const uint8_t *
                                    { return this->peerStorage.get(index); });

            io.setOnResetRequest([this]()
                                 {
                                     this->peerStorage.clear();
                                     this->espnowMIDI.clearPeers();

                                     //  esp_now_deinit();
                                     //  delay(10);
                                     //  esp_now_init();
                                 });

#ifdef HAS_USB_MIDI
            TinyUSBDevice.setManufacturerDescriptor("grantler instruments");
            TinyUSBDevice.setProductDescriptor("enomik3000_client");

            g_client_usb_midi.begin();

            if (TinyUSBDevice.mounted())
            {
                TinyUSBDevice.detach();
                delay(10);
            }
            TinyUSBDevice.attach();

            CLIENT_USBMIDI.begin(MIDI_CHANNEL_OMNI);
            CLIENT_USBMIDI.turnThruOff();

            EspNowMidiLog::i("USB MIDI initialized");
#endif

            // Initialize ESP-NOW MIDI (apply persisted power-save preference)
            if (!espnowMIDI.begin(io.isPowerSave()))
            {
                EspNowMidiLog::e("Failed to initialize ESP-NOW MIDI");
                return false;
            }

            // --- Set handlers for ESP-NOW ---

#ifdef HAS_USB_MIDI
            // --- Set handlers for USB MIDI ---
            CLIENT_USBMIDI.setHandleSystemExclusive(handleSysExStatic);
            CLIENT_USBMIDI.setHandleNoteOn(handleNoteOnStatic);
            CLIENT_USBMIDI.setHandleNoteOff(handleNoteOffStatic);
            CLIENT_USBMIDI.setHandleControlChange(handleControlChangeStatic);
            CLIENT_USBMIDI.setHandleProgramChange(handleProgramChangeStatic);
            CLIENT_USBMIDI.setHandleAfterTouchChannel(handleAfterTouchChannelStatic);
            CLIENT_USBMIDI.setHandleAfterTouchPoly(handleAfterTouchPolyStatic);
            CLIENT_USBMIDI.setHandlePitchBend(handlePitchBendStatic);
            CLIENT_USBMIDI.setHandleStart(handleStartStatic);
            CLIENT_USBMIDI.setHandleStop(handleStopStatic);
            CLIENT_USBMIDI.setHandleContinue(handleContinueStatic);
            CLIENT_USBMIDI.setHandleClock(handleClockStatic);
            CLIENT_USBMIDI.setHandleSongSelect(handleSongSelectStatic);
#endif

            // Initialize peer storage (handles EEPROM internally)
            if (!peerStorage.begin())
            {
                EspNowMidiLog::e("Failed to initialize peer storage");
                return false;
            }

            EspNowMidiLog::i("Restoring peers from storage...");
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
                            EspNowMidiLog::i("Restored peer: %s", macToString(mac).c_str());
                            restoredCount++;
                        }
                        else
                        {
                            EspNowMidiLog::e("Failed to restore peer: %s", macToString(mac).c_str());
                        }
                    }
                    else
                    {
                        EspNowMidiLog::i("Peer already exists, skipping: %s", macToString(mac).c_str());
                        skippedCount++;
                    }
                }
            }

            EspNowMidiLog::i("Peer restoration complete: %d restored, %d skipped",
                          restoredCount, skippedCount);

            isInitialized = true;
            sendHandShake();
            return true;
        }

        /**
         * @brief Processes local I/O and optional incoming USB MIDI.
         *
         * Call this from the Arduino `loop()` function.
         */
        void loop()
        {
#ifdef HAS_USB_MIDI
            CLIENT_USBMIDI.read();
#endif
            processReceived();
            io.loop();
        }

        /**
         * @brief Runs the handlers for MIDI received over ESP-NOW. Called by loop().
         *
         * Received messages wait in an inbox until then, so handlers run in the
         * loop task and react as often as loop() is called.
         */
        void processReceived()
        {
            midi_message msg;
            uint8_t from[6]; // sender, unused here
            for (int i = 0; i < MIDI_INBOX_SIZE && _inbox.pop(msg, from); ++i)
            {
                dispatch(msg);
            }
        }

        /** @return ESP-NOW messages dropped because loop() fell behind (since boot). */
        uint32_t getReceiveDropCount() const
        {
            return _inbox.droppedCount();
        }

        /**
         * @brief Where ESP-NOW receive handlers run (default: in loop()).
         *
         * `false` runs them immediately in the ESP-NOW receive callback (WiFi
         * task, on dual-core chips in parallel to loop()). Lower latency while
         * loop() blocks, but handlers then must not touch data that loop() uses,
         * and the pin configuration must not change while MIDI arrives.
         * Call before begin().
         */
        void setDispatchInLoop(bool enabled)
        {
            _dispatchInLoop = enabled;
        }

        /** @brief Sends Note On over ESP-NOW and USB when available.
         * @return `true` when the ESP-NOW send succeeds. */
        bool sendNoteOn(byte note, byte velocity, byte channel)
        {
            auto err = espnowMIDI.sendNoteOn(note, velocity, channel);
            sendToUsb(MIDI_NOTE_ON, channel, note, velocity);
            maybeLoopback([&]() { handleNoteOnStatic(channel, note, velocity); });
            return err == ESP_OK;
        }

        /** @brief Sends Note Off over ESP-NOW and USB when available.
         * @return `true` when the ESP-NOW send succeeds. */
        bool sendNoteOff(byte note, byte velocity, byte channel)
        {
            auto err = espnowMIDI.sendNoteOff(note, velocity, channel);
            sendToUsb(MIDI_NOTE_OFF, channel, note, velocity);
            maybeLoopback([&]() { handleNoteOffStatic(channel, note, velocity); });
            return err == ESP_OK;
        }

        /** @brief Sends Control Change over ESP-NOW and USB when available.
         * @return `true` when the ESP-NOW send succeeds. */
        bool sendControlChange(byte control, byte value, byte channel)
        {
            auto err = espnowMIDI.sendControlChange(control, value, channel);
            sendToUsb(MIDI_CONTROL_CHANGE, channel, control, value);
            maybeLoopback([&]() { handleControlChangeStatic(channel, control, value); });
            return err == ESP_OK;
        }

        /** @brief Sends Program Change over ESP-NOW and USB when available.
         * @return `true` when the ESP-NOW send succeeds. */
        bool sendProgramChange(byte program, byte channel)
        {
            auto err = espnowMIDI.sendProgramChange(program, channel);
            sendToUsb(MIDI_PROGRAM_CHANGE, channel, program);
            maybeLoopback([&]() { handleProgramChangeStatic(channel, program); });
            return err == ESP_OK;
        }

        /** @brief Sends channel aftertouch over ESP-NOW and USB when available.
         * @return `true` when the ESP-NOW send succeeds. */
        bool sendAfterTouch(byte pressure, byte channel)
        {
            auto err = espnowMIDI.sendAfterTouch(pressure, channel);
            sendToUsb(MIDI_AFTERTOUCH, channel, pressure);
            maybeLoopback([&]() { handleAfterTouchChannelStatic(channel, pressure); });
            return err == ESP_OK;
        }

        /** @brief Sends polyphonic aftertouch over ESP-NOW and USB when available.
         * @return `true` when the ESP-NOW send succeeds. */
        bool sendPolyAfterTouch(byte note, byte pressure, byte channel)
        {
            auto err = espnowMIDI.sendAfterTouchPoly(note, pressure, channel);
            sendToUsb(MIDI_POLY_AFTERTOUCH, channel, note, pressure);
            maybeLoopback([&]() { handleAfterTouchPolyStatic(channel, note, pressure); });
            return err == ESP_OK;
        }

        /**
         * @brief Sends signed pitch bend over ESP-NOW and USB when available.
         * @param value Pitch bend from `-8192` to `8191`; `0` is center.
         * @param channel MIDI channel.
         * @return `true` when the ESP-NOW send succeeds.
         */
        bool sendPitchBend(int value, byte channel) // signed; center = 0
        {
            auto err = espnowMIDI.sendPitchBend(value, channel);
            sendPitchBendToUsb(value, channel);
            maybeLoopback([&]() { handlePitchBendStatic(channel, value); });
            return err == ESP_OK;
        }

        /** @brief Sends MIDI Start. @return `true` when the ESP-NOW send succeeds. */
        bool sendStart()
        {
            auto err = espnowMIDI.sendStart();
            sendToUsb(MIDI_START);
            maybeLoopback([&]() { handleStartStatic(); });
            return err == ESP_OK;
        }

        /** @brief Sends MIDI Stop. @return `true` when the ESP-NOW send succeeds. */
        bool sendStop()
        {
            auto err = espnowMIDI.sendStop();
            sendToUsb(MIDI_STOP);
            maybeLoopback([&]() { handleStopStatic(); });
            return err == ESP_OK;
        }

        /** @brief Sends MIDI Continue. @return `true` when the ESP-NOW send succeeds. */
        bool sendContinue()
        {
            auto err = espnowMIDI.sendContinue();
            sendToUsb(MIDI_CONTINUE);
            maybeLoopback([&]() { handleContinueStatic(); });
            return err == ESP_OK;
        }

        /** @brief Sends MIDI Timing Clock. @return `true` when the ESP-NOW send succeeds. */
        bool sendClock()
        {
            auto err = espnowMIDI.sendClock();
            sendToUsb(MIDI_TIME_CLOCK);
            maybeLoopback([&]() { handleClockStatic(); });
            return err == ESP_OK;
        }

        /** @brief Sends Song Position Pointer. @return `true` when the ESP-NOW send succeeds. */
        bool sendSongPosition(uint16_t value)
        {
            auto err = espnowMIDI.sendSongPosition(value);
            sendToUsb(MIDI_SONG_POS_POINTER, 0, value & 0x7F, (value >> 7) & 0x7F);
            maybeLoopback([&]() { handleSongPositionStatic(value); });
            return err == ESP_OK;
        }

        /** @brief Sends Song Select. @return `true` when the ESP-NOW send succeeds. */
        bool sendSongSelect(uint8_t value)
        {
            auto err = espnowMIDI.sendSongSelect(value);
            sendToUsb(MIDI_SONG_SELECT, 0, value);
            maybeLoopback([&]() { handleSongSelectStatic(value); });
            return err == ESP_OK;
        }

        /** @brief Sends a complete SysEx buffer, including `F0` and `F7`.
         * @return `true` when the ESP-NOW send succeeds. */
        bool sendSysEx(const uint8_t *data, uint16_t length)
        {
            auto err = espnowMIDI.sendSysex((uint8_t *)data, length);
            sendSysExToUsb(data, length);
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

        void setHandlePitchBend(std::function<void(byte channel, int value)> handler) // signed; center = 0
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
                EspNowMidiLog::e("Client not initialized. Cannot send handshake.");
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
                        EspNowMidiLog::e("Failed to send handshake to: %s", macToString(mac).c_str());
                    }
                }
            }
        }

        // Simplified peer management - delegates to PeerStorage
        bool addPeer(const uint8_t mac[6])
        {
            if (!isInitialized)
            {
                EspNowMidiLog::e("Client not initialized. Call begin() first.");
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
                EspNowMidiLog::e("Failed to add peer to ESP-NOW");
                // Rollback storage change
                peerStorage.remove(mac);
                return false;
            }

            return true;
        }

        bool addPeerFromString(const PortableString &macStr)
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

        PortableString getMacString(int index)
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