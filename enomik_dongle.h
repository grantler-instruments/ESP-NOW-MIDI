#pragma once

#include "esp_now_midi.h"
#include "MidiMessageHistory.h"
#include "UsbMidiQueue.h"
#include "utils/esp.h"
#include "utils/mac.h"
#include "version.h"
#include <WiFi.h>
#include <esp_system.h>
#include <functional>

#ifndef DONGLE_MAX_HISTORY
#define DONGLE_MAX_HISTORY 5
#endif

#ifndef DONGLE_UPDATE_DISPLAY_INTERVAL_MS
#define DONGLE_UPDATE_DISPLAY_INTERVAL_MS 64
#endif

#ifdef HAS_USB_MIDI
#include <Adafruit_TinyUSB.h>
#include <MIDI.h>

// Global USB MIDI objects — MUST be at file scope; distinct from Client symbols.
Adafruit_USBD_MIDI g_dongle_usb_midi;
MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, g_dongle_usb_midi, DONGLE_USBMIDI);
#endif

namespace enomik
{
    /**
     * @brief USB MIDI ↔ ESP-NOW MIDI bridge for a host-connected dongle board.
     *
     * Call begin() once and loop() regularly from the Arduino loop. Optionally
     * register a Display implementation with setDisplay() for status UI.
     *
     * Requires a native-USB chip (ESP32-S2 / ESP32-S3) with TinyUSB.
     */
    class Dongle
    {
    public:
        /**
         * @brief Optional status display driven by the dongle.
         *
         * Subclass, implement begin()/update(), and register with setDisplay().
         */
        class Display
        {
        public:
            virtual ~Display() = default;

            /** @brief Initialize the display hardware. @return false on failure. */
            virtual bool begin() = 0;

            /**
             * @brief Redraw status UI.
             * @param mac Local STA MAC (6 bytes).
             * @param version Library version string.
             * @param peerCount Current ESP-NOW peer count.
             * @param usbStatus One of D/S/Q/C (disconnected / suspended / queued / connected).
             * @param history Ring buffer of recent bridged messages.
             * @param historySize Capacity of history.
             * @param historyHead Next write index (oldest entry when buffer is full).
             */
            virtual void update(
                const uint8_t mac[6],
                const char *version,
                int peerCount,
                char usbStatus,
                const MidiMessageHistory *history,
                int historySize,
                int historyHead) = 0;
        };

        static Dongle *instancePtr; ///< Active dongle used by static receive callbacks.
        esp_now_midi espnowMIDI;    ///< Underlying ESP-NOW MIDI transport.

        /**
         * @brief Bridge filter callback.
         *
         * Receives a mutable `midi_message` (`status`, `channel` 1–16, `firstByte`,
         * `secondByte`). Return `true` to forward (after any in-place edits), or
         * `false` to drop. Keep the body non-blocking (no `delay`, avoid heavy Serial).
         * Does not apply to `send*` inject APIs.
         */
        using BridgeFilter = std::function<bool(midi_message &)>;

        /** @brief Constructs the dongle and makes it the active callback instance. */
        Dongle()
            : _isInitialized(false),
              _usbMidiInitialized(false),
              _display(nullptr),
              _lastDisplayUpdate(0),
              _displayIntervalMs(DONGLE_UPDATE_DISPLAY_INTERVAL_MS),
              _messageIndex(0),
              _manufacturer("grantler instruments"),
              _product("enomik3000_dongle"),
              _version(getVersion())
        {
            memset(_baseMac, 0, sizeof(_baseMac));
            memset(_messageHistory, 0, sizeof(_messageHistory));
            instancePtr = this;
        }

        /**
         * @brief Filter messages from ESP-NOW peers toward the USB host (computer).
         * Pass nullptr to clear. Unset = transparent bridge.
         */
        void setToHostFilter(BridgeFilter filter)
        {
            _toHostFilter = filter;
        }

        /**
         * @brief Filter messages from the USB host (computer) toward ESP-NOW peers.
         * Pass nullptr to clear. Unset = transparent bridge.
         */
        void setFromHostFilter(BridgeFilter filter)
        {
            _fromHostFilter = filter;
        }

        /**
         * @brief Register an optional display. Pass nullptr to disable.
         *
         * Call before begin(), or after begin() if the display is ready later
         * (begin() will be invoked on the display when set after init).
         */
        void setDisplay(Display *display)
        {
            _display = display;
            if (_isInitialized && _display)
            {
                if (!_display->begin())
                {
                    Serial.println("Display init failed");
                    _display = nullptr;
                }
            }
        }

        /** @brief Minimum interval between display updates (milliseconds). */
        void setDisplayUpdateInterval(uint32_t intervalMs)
        {
            _displayIntervalMs = intervalMs;
        }

        /** @brief USB manufacturer string; call before begin(). */
        void setManufacturerDescriptor(const char *manufacturer)
        {
            if (manufacturer)
            {
                _manufacturer = manufacturer;
            }
        }

        /** @brief USB product string; call before begin(). */
        void setProductDescriptor(const char *product)
        {
            if (product)
            {
                _product = product;
            }
        }

        /**
         * @brief Initializes USB MIDI, ESP-NOW, and an optional registered display.
         * @return `true` when the bridge is ready.
         */
        bool begin()
        {
#ifndef HAS_USB_MIDI
            Serial.println("enomik::Dongle requires a USB-capable chip (ESP32-S2/S3)");
            return false;
#else
            Serial.println("=== ESP-NOW MIDI DONGLE ===");
            Serial.printf("ESP-IDF Version: %s\n", esp_get_idf_version());
            Serial.printf("Channel: %d\n", ESP_NOW_MIDI_CHANNEL);

            TinyUSBDevice.setManufacturerDescriptor(_manufacturer);
            TinyUSBDevice.setProductDescriptor(_product);

            g_dongle_usb_midi.begin();

            if (TinyUSBDevice.mounted())
            {
                TinyUSBDevice.detach();
                delay(100);
            }
            TinyUSBDevice.attach();

            if (!espnowMIDI.begin())
            {
                Serial.println("Failed to initialize ESP-NOW MIDI");
                return false;
            }

            readMacAddress();
            Serial.print("Mac: ");
            Serial.println(macToString(_baseMac));

            espnowMIDI.setHandleNoteOn(handleNoteOnStatic);
            espnowMIDI.setHandleNoteOff(handleNoteOffStatic);
            espnowMIDI.setHandleControlChange(handleControlChangeStatic);
            espnowMIDI.setHandleProgramChange(handleProgramChangeStatic);
            espnowMIDI.setHandlePitchBend(handlePitchBendStatic);
            espnowMIDI.setHandleAfterTouchChannel(handleAfterTouchChannelStatic);
            espnowMIDI.setHandleAfterTouchPoly(handleAfterTouchPolyStatic);
            espnowMIDI.setHandleStart(handleStartStatic);
            espnowMIDI.setHandleStop(handleStopStatic);
            espnowMIDI.setHandleContinue(handleContinueStatic);
            espnowMIDI.setHandleClock(handleClockStatic);
            espnowMIDI.setHandleSongPosition(handleSongPositionStatic);
            espnowMIDI.setHandleSongSelect(handleSongSelectStatic);

            Serial.print("Registered peers: ");
            Serial.println(espnowMIDI.getPeersCount());

            if (_display)
            {
                if (!_display->begin())
                {
                    Serial.println("Display init failed");
                    _display = nullptr;
                }
            }

            _isInitialized = true;
            Serial.println("Setup complete - ready!");
            return true;
#endif
        }

        /**
         * @brief Processes USB MIDI, drains the USB TX queue, and refreshes the display.
         *
         * Call this from the Arduino `loop()` function.
         */
        void loop()
        {
#ifdef HAS_USB_MIDI
            if (!_isInitialized)
            {
                return;
            }

            const unsigned long now = millis();

            if (_usbMidiInitialized && !TinyUSBDevice.mounted())
            {
                Serial.println("USB disconnected");
                _usbMidiInitialized = false;
                _usbMidiQueue.clear();
            }

            if (!_usbMidiInitialized && TinyUSBDevice.mounted())
            {
                Serial.println("USB mounted - initializing MIDI");

                DONGLE_USBMIDI.begin(MIDI_CHANNEL_OMNI);
                DONGLE_USBMIDI.turnThruOff();

                DONGLE_USBMIDI.setHandleNoteOn(onNoteOnStatic);
                DONGLE_USBMIDI.setHandleNoteOff(onNoteOffStatic);
                DONGLE_USBMIDI.setHandleControlChange(onControlChangeStatic);
                DONGLE_USBMIDI.setHandleProgramChange(onProgramChangeStatic);
                DONGLE_USBMIDI.setHandlePitchBend(onPitchBendStatic);
                DONGLE_USBMIDI.setHandleAfterTouchChannel(onAfterTouchStatic);
                DONGLE_USBMIDI.setHandleAfterTouchPoly(onPolyAfterTouchStatic);
                DONGLE_USBMIDI.setHandleStart(onStartStatic);
                DONGLE_USBMIDI.setHandleStop(onStopStatic);
                DONGLE_USBMIDI.setHandleContinue(onContinueStatic);
                DONGLE_USBMIDI.setHandleClock(onClockStatic);
                DONGLE_USBMIDI.setHandleSongPosition(onSongPositionStatic);
                DONGLE_USBMIDI.setHandleSongSelect(onSongSelectStatic);

                _usbMidiInitialized = true;
                Serial.println("USB MIDI ready!");
            }

            if (_usbMidiInitialized)
            {
                DONGLE_USBMIDI.read();
                drainUsbMidiQueue();
            }

            logUsbState(now);
            updateDisplay(now);
#endif
        }

        /** @return USB status char: D disconnected, S suspended, Q queued, C connected. */
        char getUsbStatusChar()
        {
#ifdef HAS_USB_MIDI
            if (!TinyUSBDevice.mounted())
            {
                return 'D';
            }
            if (TinyUSBDevice.suspended())
            {
                return 'S';
            }
            if (_usbMidiQueue.hasPending())
            {
                return 'Q';
            }
            return 'C';
#else
            return 'D';
#endif
        }

        /** @return true when USB is mounted and MIDI handlers are registered. */
        bool isUsbReady() const
        {
            return _usbMidiInitialized;
        }

        /** @return Local STA MAC as a colon-separated hex string. */
        String getMacAddress() const
        {
            return macToString(_baseMac);
        }

        /** @return Pointer to the 6-byte local STA MAC. */
        const uint8_t *getMac() const
        {
            return _baseMac;
        }

        int getPeersCount()
        {
            return espnowMIDI.getPeersCount();
        }

        bool addPeer(const uint8_t mac[6])
        {
            return espnowMIDI.addPeer(mac);
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

        // --- Inject MIDI (ESP-NOW + USB when ready) ---

        bool sendNoteOn(byte note, byte velocity, byte channel)
        {
            midi_message msg;
            msg.status = MIDI_NOTE_ON;
            msg.channel = channel;
            msg.firstByte = note;
            msg.secondByte = velocity;
            queueToUsb(msg, true);
            return espnowMIDI.sendNoteOn(note, velocity, channel) == ESP_OK;
        }

        bool sendNoteOff(byte note, byte velocity, byte channel)
        {
            midi_message msg;
            msg.status = MIDI_NOTE_OFF;
            msg.channel = channel;
            msg.firstByte = note;
            msg.secondByte = velocity;
            queueToUsb(msg, true);
            return espnowMIDI.sendNoteOff(note, velocity, channel) == ESP_OK;
        }

        bool sendControlChange(byte control, byte value, byte channel)
        {
            midi_message msg;
            msg.status = MIDI_CONTROL_CHANGE;
            msg.channel = channel;
            msg.firstByte = control;
            msg.secondByte = value;
            queueToUsb(msg, true);
            return espnowMIDI.sendControlChange(control, value, channel) == ESP_OK;
        }

        bool sendProgramChange(byte program, byte channel)
        {
            midi_message msg;
            msg.status = MIDI_PROGRAM_CHANGE;
            msg.channel = channel;
            msg.firstByte = program;
            msg.secondByte = 0;
            queueToUsb(msg, true);
            return espnowMIDI.sendProgramChange(program, channel) == ESP_OK;
        }

        bool sendAfterTouch(byte pressure, byte channel)
        {
            midi_message msg;
            msg.status = MIDI_AFTERTOUCH;
            msg.channel = channel;
            msg.firstByte = pressure;
            msg.secondByte = 0;
            queueToUsb(msg, true);
            return espnowMIDI.sendAfterTouch(pressure, channel) == ESP_OK;
        }

        bool sendPolyAfterTouch(byte note, byte pressure, byte channel)
        {
            midi_message msg;
            msg.status = MIDI_POLY_AFTERTOUCH;
            msg.channel = channel;
            msg.firstByte = note;
            msg.secondByte = pressure;
            queueToUsb(msg, true);
            return espnowMIDI.sendAfterTouchPoly(note, pressure, channel) == ESP_OK;
        }

        bool sendPitchBend(int value, byte channel)
        {
            midi_message msg;
            msg.status = MIDI_PITCH_BEND;
            msg.channel = channel;
            const int unsignedValue = value + 8192;
            msg.firstByte = unsignedValue & 0x7F;
            msg.secondByte = (unsignedValue >> 7) & 0x7F;
            queueToUsb(msg, true);
            return espnowMIDI.sendPitchBend(value, channel) == ESP_OK;
        }

        bool sendStart()
        {
            midi_message msg;
            msg.status = MIDI_START;
            msg.channel = 0;
            msg.firstByte = 0;
            msg.secondByte = 0;
            queueToUsb(msg, true);
            return espnowMIDI.sendStart() == ESP_OK;
        }

        bool sendStop()
        {
            midi_message msg;
            msg.status = MIDI_STOP;
            msg.channel = 0;
            msg.firstByte = 0;
            msg.secondByte = 0;
            queueToUsb(msg, true);
            return espnowMIDI.sendStop() == ESP_OK;
        }

        bool sendContinue()
        {
            midi_message msg;
            msg.status = MIDI_CONTINUE;
            msg.channel = 0;
            msg.firstByte = 0;
            msg.secondByte = 0;
            queueToUsb(msg, true);
            return espnowMIDI.sendContinue() == ESP_OK;
        }

        bool sendClock()
        {
            _usbMidiQueue.enqueueClock();
            return espnowMIDI.sendClock() == ESP_OK;
        }

        bool sendSongPosition(uint16_t value)
        {
            midi_message msg;
            msg.status = MIDI_SONG_POS_POINTER;
            msg.channel = 0;
            msg.firstByte = value & 0x7F;
            msg.secondByte = (value >> 7) & 0x7F;
            queueToUsb(msg, false);
            return espnowMIDI.sendSongPosition(value) == ESP_OK;
        }

        bool sendSongSelect(uint8_t value)
        {
            midi_message msg;
            msg.status = MIDI_SONG_SELECT;
            msg.channel = 0;
            msg.firstByte = value;
            msg.secondByte = 0;
            queueToUsb(msg, true);
            return espnowMIDI.sendSongSelect(value) == ESP_OK;
        }

    private:
        bool _isInitialized;
        bool _usbMidiInitialized;
        Display *_display;
        uint32_t _lastDisplayUpdate;
        uint32_t _displayIntervalMs;
        UsbMidiQueue _usbMidiQueue;
        MidiMessageHistory _messageHistory[DONGLE_MAX_HISTORY];
        int _messageIndex;
        uint8_t _baseMac[6];
        const char *_manufacturer;
        const char *_product;
        String _version;
        BridgeFilter _toHostFilter;
        BridgeFilter _fromHostFilter;

        void addToHistory(const midi_message &msg, bool outgoing)
        {
            _messageHistory[_messageIndex].message = msg;
            _messageHistory[_messageIndex].outgoing = outgoing;
            _messageHistory[_messageIndex].timestamp = millis();
            _messageIndex = (_messageIndex + 1) % DONGLE_MAX_HISTORY;
        }

        void queueToUsb(const midi_message &msg, bool addHistory)
        {
            if (addHistory)
            {
                addToHistory(msg, false);
            }
            _usbMidiQueue.enqueue(msg);
        }

        /** ESP-NOW → USB host. Runs toHost filter, then queues (clock coalesced). */
        void bridgeToHost(midi_message &msg, bool addHistory = true)
        {
            if (_toHostFilter && !_toHostFilter(msg))
            {
                return;
            }
            if (msg.status == MIDI_TIME_CLOCK)
            {
                _usbMidiQueue.enqueueClock();
                return;
            }
            queueToUsb(msg, addHistory);
        }

        /** USB host → ESP-NOW. Runs fromHost filter, then history + send. */
        void bridgeFromHost(midi_message &msg, bool addHistory = true)
        {
            if (_fromHostFilter && !_fromHostFilter(msg))
            {
                return;
            }
            if (addHistory)
            {
                addToHistory(msg, true);
            }
            dispatchToEspNow(msg);
        }

        void dispatchToEspNow(const midi_message &msg)
        {
            switch (msg.status)
            {
            case MIDI_NOTE_ON:
                espnowMIDI.sendNoteOn(msg.firstByte, msg.secondByte, msg.channel);
                break;
            case MIDI_NOTE_OFF:
                espnowMIDI.sendNoteOff(msg.firstByte, msg.secondByte, msg.channel);
                break;
            case MIDI_CONTROL_CHANGE:
                espnowMIDI.sendControlChange(msg.firstByte, msg.secondByte, msg.channel);
                break;
            case MIDI_PROGRAM_CHANGE:
                espnowMIDI.sendProgramChange(msg.firstByte, msg.channel);
                break;
            case MIDI_AFTERTOUCH:
                espnowMIDI.sendAfterTouch(msg.firstByte, msg.channel);
                break;
            case MIDI_POLY_AFTERTOUCH:
                espnowMIDI.sendAfterTouchPoly(msg.firstByte, msg.secondByte, msg.channel);
                break;
            case MIDI_PITCH_BEND:
            {
                const int value = ((msg.secondByte << 7) | msg.firstByte) - 8192;
                espnowMIDI.sendPitchBend(value, msg.channel);
                break;
            }
            case MIDI_START:
                espnowMIDI.sendStart();
                break;
            case MIDI_STOP:
                espnowMIDI.sendStop();
                break;
            case MIDI_CONTINUE:
                espnowMIDI.sendContinue();
                break;
            case MIDI_TIME_CLOCK:
                espnowMIDI.sendClock();
                break;
            case MIDI_SONG_POS_POINTER:
            {
                const uint16_t pos = (msg.secondByte << 7) | msg.firstByte;
                espnowMIDI.sendSongPosition(pos);
                break;
            }
            case MIDI_SONG_SELECT:
                espnowMIDI.sendSongSelect(msg.firstByte);
                break;
            default:
                break;
            }
        }

        void readMacAddress()
        {
            esp_err_t ret = esp_wifi_get_mac(WIFI_IF_STA, _baseMac);
            if (ret != ESP_OK)
            {
                Serial.println("Failed to read MAC address");
            }
        }

#ifdef HAS_USB_MIDI
        bool sendQueuedMidi(const midi_message &msg)
        {
            const uint8_t ch = (msg.channel - 1) & 0x0F;
            uint8_t packet[4] = {0, 0, 0, 0};

            switch (msg.status)
            {
            case MIDI_NOTE_ON:
                packet[0] = 0x09;
                packet[1] = MIDI_NOTE_ON | ch;
                packet[2] = msg.firstByte;
                packet[3] = msg.secondByte;
                break;
            case MIDI_NOTE_OFF:
                packet[0] = 0x08;
                packet[1] = MIDI_NOTE_OFF | ch;
                packet[2] = msg.firstByte;
                packet[3] = msg.secondByte;
                break;
            case MIDI_CONTROL_CHANGE:
                packet[0] = 0x0B;
                packet[1] = MIDI_CONTROL_CHANGE | ch;
                packet[2] = msg.firstByte;
                packet[3] = msg.secondByte;
                break;
            case MIDI_PROGRAM_CHANGE:
                packet[0] = 0x0C;
                packet[1] = MIDI_PROGRAM_CHANGE | ch;
                packet[2] = msg.firstByte;
                break;
            case MIDI_AFTERTOUCH:
                packet[0] = 0x0D;
                packet[1] = MIDI_AFTERTOUCH | ch;
                packet[2] = msg.firstByte;
                break;
            case MIDI_POLY_AFTERTOUCH:
                packet[0] = 0x0A;
                packet[1] = MIDI_POLY_AFTERTOUCH | ch;
                packet[2] = msg.firstByte;
                packet[3] = msg.secondByte;
                break;
            case MIDI_PITCH_BEND:
                packet[0] = 0x0E;
                packet[1] = MIDI_PITCH_BEND | ch;
                packet[2] = msg.firstByte;
                packet[3] = msg.secondByte;
                break;
            case MIDI_START:
                packet[0] = 0x0F;
                packet[1] = MIDI_START;
                break;
            case MIDI_STOP:
                packet[0] = 0x0F;
                packet[1] = MIDI_STOP;
                break;
            case MIDI_CONTINUE:
                packet[0] = 0x0F;
                packet[1] = MIDI_CONTINUE;
                break;
            case MIDI_TIME_CLOCK:
                packet[0] = 0x0F;
                packet[1] = MIDI_TIME_CLOCK;
                break;
            case MIDI_SONG_POS_POINTER:
                packet[0] = 0x03;
                packet[1] = MIDI_SONG_POS_POINTER;
                packet[2] = msg.firstByte;
                packet[3] = msg.secondByte;
                break;
            case MIDI_SONG_SELECT:
                packet[0] = 0x02;
                packet[1] = MIDI_SONG_SELECT;
                packet[2] = msg.firstByte;
                break;
            default:
                return true;
            }

            return g_dongle_usb_midi.writePacket(packet);
        }

        void drainUsbMidiQueue()
        {
            if (!TinyUSBDevice.mounted())
            {
                return;
            }

            if (TinyUSBDevice.suspended())
            {
                if (_usbMidiQueue.hasPending())
                {
                    TinyUSBDevice.remoteWakeup();
                }
                return;
            }

            if (!TinyUSBDevice.ready())
            {
                return;
            }

            midi_message msg;
            while (_usbMidiQueue.peek(msg))
            {
                if (!sendQueuedMidi(msg))
                {
                    break;
                }
                _usbMidiQueue.consumeHead();
            }
        }

        void logUsbState(unsigned long now)
        {
            static char lastStatus = 0;
            static uint32_t lastLogMs = 0;
            const char status = getUsbStatusChar();

            if (status == lastStatus && (status == 'C' || (now - lastLogMs) < 10000))
            {
                return;
            }

            lastStatus = status;
            lastLogMs = now;
            Serial.printf("USB status=%c mounted=%d suspended=%d ready=%d queue=%u\n",
                          status,
                          TinyUSBDevice.mounted(),
                          TinyUSBDevice.suspended(),
                          TinyUSBDevice.ready(),
                          _usbMidiQueue.pendingCount());
        }
#endif

        void updateDisplay(unsigned long now)
        {
            if (!_display || (now - _lastDisplayUpdate) < _displayIntervalMs)
            {
                return;
            }
            _lastDisplayUpdate = now;
            _display->update(
                _baseMac,
                _version.c_str(),
                espnowMIDI.getPeersCount(),
                getUsbStatusChar(),
                _messageHistory,
                DONGLE_MAX_HISTORY,
                _messageIndex);
        }

        // --- ESP-NOW → USB host ---

        static void handleNoteOnStatic(byte channel, byte note, byte velocity)
        {
            if (!instancePtr)
                return;
            midi_message msg;
            msg.status = MIDI_NOTE_ON;
            msg.channel = channel;
            msg.firstByte = note;
            msg.secondByte = velocity;
            instancePtr->bridgeToHost(msg);
        }

        static void handleNoteOffStatic(byte channel, byte note, byte velocity)
        {
            if (!instancePtr)
                return;
            midi_message msg;
            msg.status = MIDI_NOTE_OFF;
            msg.channel = channel;
            msg.firstByte = note;
            msg.secondByte = velocity;
            instancePtr->bridgeToHost(msg);
        }

        static void handleControlChangeStatic(byte channel, byte control, byte value)
        {
            if (!instancePtr)
                return;
            midi_message msg;
            msg.status = MIDI_CONTROL_CHANGE;
            msg.channel = channel;
            msg.firstByte = control;
            msg.secondByte = value;
            instancePtr->bridgeToHost(msg);
        }

        static void handleProgramChangeStatic(byte channel, byte program)
        {
            if (!instancePtr)
                return;
            midi_message msg;
            msg.status = MIDI_PROGRAM_CHANGE;
            msg.channel = channel;
            msg.firstByte = program;
            msg.secondByte = 0;
            instancePtr->bridgeToHost(msg);
        }

        static void handleAfterTouchChannelStatic(byte channel, byte pressure)
        {
            if (!instancePtr)
                return;
            midi_message msg;
            msg.status = MIDI_AFTERTOUCH;
            msg.channel = channel;
            msg.firstByte = pressure;
            msg.secondByte = 0;
            instancePtr->bridgeToHost(msg);
        }

        static void handleAfterTouchPolyStatic(byte channel, byte note, byte pressure)
        {
            if (!instancePtr)
                return;
            midi_message msg;
            msg.status = MIDI_POLY_AFTERTOUCH;
            msg.channel = channel;
            msg.firstByte = note;
            msg.secondByte = pressure;
            instancePtr->bridgeToHost(msg);
        }

        static void handlePitchBendStatic(byte channel, int value)
        {
            if (!instancePtr)
                return;
            midi_message msg;
            msg.status = MIDI_PITCH_BEND;
            msg.channel = channel;
            const int unsignedValue = value + 8192;
            msg.firstByte = unsignedValue & 0x7F;
            msg.secondByte = (unsignedValue >> 7) & 0x7F;
            instancePtr->bridgeToHost(msg);
        }

        static void handleStartStatic()
        {
            if (!instancePtr)
                return;
            midi_message msg;
            msg.status = MIDI_START;
            msg.channel = 0;
            msg.firstByte = 0;
            msg.secondByte = 0;
            instancePtr->bridgeToHost(msg);
        }

        static void handleStopStatic()
        {
            if (!instancePtr)
                return;
            midi_message msg;
            msg.status = MIDI_STOP;
            msg.channel = 0;
            msg.firstByte = 0;
            msg.secondByte = 0;
            instancePtr->bridgeToHost(msg);
        }

        static void handleContinueStatic()
        {
            if (!instancePtr)
                return;
            midi_message msg;
            msg.status = MIDI_CONTINUE;
            msg.channel = 0;
            msg.firstByte = 0;
            msg.secondByte = 0;
            instancePtr->bridgeToHost(msg);
        }

        static void handleClockStatic()
        {
            if (!instancePtr)
                return;
            midi_message msg;
            msg.status = MIDI_TIME_CLOCK;
            msg.channel = 0;
            msg.firstByte = 0;
            msg.secondByte = 0;
            instancePtr->bridgeToHost(msg, false);
        }

        static void handleSongPositionStatic(uint16_t value)
        {
            if (!instancePtr)
                return;
            midi_message msg;
            msg.status = MIDI_SONG_POS_POINTER;
            msg.channel = 0;
            msg.firstByte = value & 0x7F;
            msg.secondByte = (value >> 7) & 0x7F;
            instancePtr->bridgeToHost(msg, false);
        }

        static void handleSongSelectStatic(byte value)
        {
            if (!instancePtr)
                return;
            midi_message msg;
            msg.status = MIDI_SONG_SELECT;
            msg.channel = 0;
            msg.firstByte = value;
            msg.secondByte = 0;
            instancePtr->bridgeToHost(msg);
        }

        // --- USB host → ESP-NOW ---

        static void onNoteOnStatic(byte channel, byte pitch, byte velocity)
        {
            if (!instancePtr)
                return;
            midi_message msg;
            msg.status = MIDI_NOTE_ON;
            msg.channel = channel;
            msg.firstByte = pitch;
            msg.secondByte = velocity;
            instancePtr->bridgeFromHost(msg);
        }

        static void onNoteOffStatic(byte channel, byte pitch, byte velocity)
        {
            if (!instancePtr)
                return;
            midi_message msg;
            msg.status = MIDI_NOTE_OFF;
            msg.channel = channel;
            msg.firstByte = pitch;
            msg.secondByte = velocity;
            instancePtr->bridgeFromHost(msg);
        }

        static void onControlChangeStatic(byte channel, byte controller, byte value)
        {
            if (!instancePtr)
                return;
            midi_message msg;
            msg.status = MIDI_CONTROL_CHANGE;
            msg.channel = channel;
            msg.firstByte = controller;
            msg.secondByte = value;
            instancePtr->bridgeFromHost(msg);
        }

        static void onProgramChangeStatic(byte channel, byte program)
        {
            if (!instancePtr)
                return;
            midi_message msg;
            msg.status = MIDI_PROGRAM_CHANGE;
            msg.channel = channel;
            msg.firstByte = program;
            msg.secondByte = 0;
            instancePtr->bridgeFromHost(msg);
        }

        static void onAfterTouchStatic(byte channel, byte pressure)
        {
            if (!instancePtr)
                return;
            midi_message msg;
            msg.status = MIDI_AFTERTOUCH;
            msg.channel = channel;
            msg.firstByte = pressure;
            msg.secondByte = 0;
            instancePtr->bridgeFromHost(msg);
        }

        static void onPolyAfterTouchStatic(byte channel, byte note, byte pressure)
        {
            if (!instancePtr)
                return;
            midi_message msg;
            msg.status = MIDI_POLY_AFTERTOUCH;
            msg.channel = channel;
            msg.firstByte = note;
            msg.secondByte = pressure;
            instancePtr->bridgeFromHost(msg);
        }

        static void onPitchBendStatic(byte channel, int value)
        {
            if (!instancePtr)
                return;
            midi_message msg;
            msg.status = MIDI_PITCH_BEND;
            msg.channel = channel;
            const int raw = value + 8192;
            msg.firstByte = raw & 0x7F;
            msg.secondByte = (raw >> 7) & 0x7F;
            instancePtr->bridgeFromHost(msg);
        }

        static void onStartStatic()
        {
            if (!instancePtr)
                return;
            midi_message msg;
            msg.status = MIDI_START;
            msg.channel = 0;
            msg.firstByte = 0;
            msg.secondByte = 0;
            instancePtr->bridgeFromHost(msg);
        }

        static void onStopStatic()
        {
            if (!instancePtr)
                return;
            midi_message msg;
            msg.status = MIDI_STOP;
            msg.channel = 0;
            msg.firstByte = 0;
            msg.secondByte = 0;
            instancePtr->bridgeFromHost(msg);
        }

        static void onContinueStatic()
        {
            if (!instancePtr)
                return;
            midi_message msg;
            msg.status = MIDI_CONTINUE;
            msg.channel = 0;
            msg.firstByte = 0;
            msg.secondByte = 0;
            instancePtr->bridgeFromHost(msg);
        }

        static void onClockStatic()
        {
            if (!instancePtr)
                return;
            midi_message msg;
            msg.status = MIDI_TIME_CLOCK;
            msg.channel = 0;
            msg.firstByte = 0;
            msg.secondByte = 0;
            instancePtr->bridgeFromHost(msg, false);
        }

        static void onSongPositionStatic(unsigned int value)
        {
            if (!instancePtr)
                return;
            midi_message msg;
            msg.status = MIDI_SONG_POS_POINTER;
            msg.channel = 0;
            msg.firstByte = value & 0x7F;
            msg.secondByte = (value >> 7) & 0x7F;
            instancePtr->bridgeFromHost(msg, false);
        }

        static void onSongSelectStatic(byte value)
        {
            if (!instancePtr)
                return;
            midi_message msg;
            msg.status = MIDI_SONG_SELECT;
            msg.channel = 0;
            msg.firstByte = value;
            msg.secondByte = 0;
            instancePtr->bridgeFromHost(msg);
        }
    };

    Dongle *Dongle::instancePtr = nullptr;
} // namespace enomik
