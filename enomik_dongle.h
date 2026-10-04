#pragma once

#include "esp_now_midi.h"
#include "include/PeerStorage.h"
#include "include/PeerMuteList.h"
#include "include/MidiMessageHistory.h"
#include "include/UsbMidiQueue.h"
#include "include/UsbStallWatchdog.h"
#include "include/esp_now_midi_compat.h"
#include "utils/esp.h"
#include "utils/mac.h"
#include "include/version.h"
#include "include/esp_now_midi_prefs.h"
#ifdef ARDUINO
#include <WiFi.h>
#endif
#include <esp_system.h>
#include <functional>

#ifndef DONGLE_MAX_HISTORY
#define DONGLE_MAX_HISTORY 5
#endif

#ifndef DONGLE_UPDATE_DISPLAY_INTERVAL_MS
#define DONGLE_UPDATE_DISPLAY_INTERVAL_MS 64
#endif

#ifdef HAS_USB_MIDI
#ifdef ARDUINO
#include <Adafruit_TinyUSB.h>
#include <MIDI.h>

// Global USB MIDI objects — MUST be at file scope; distinct from Client symbols.
Adafruit_USBD_MIDI g_dongle_usb_midi;
MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, g_dongle_usb_midi, DONGLE_USBMIDI);
#elif defined(ESP_PLATFORM)
#include "include/esp_now_midi_usb.h"

// Global USB MIDI objects — MUST be at file scope; distinct from Client symbols.
TinyUsbRawMidiClass g_dongle_usb_midi;
TinyUsbMidiClass DONGLE_USBMIDI;
#endif

// Used by the USB watchdog. Weak references: if a TinyUSB build lacks either
// symbol, the pointer is null and the watchdog falls back to queue-based
// detection instead of failing to link.
extern "C" bool usbd_edpt_busy(uint8_t rhport, uint8_t ep_addr) __attribute__((weak));
extern "C" uint8_t const *tud_descriptor_configuration_cb(uint8_t index) __attribute__((weak));
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
              _displayDirty(true),
              _lastDrawnPeerCount(-1),
              _lastDrawnUsbStatus(0),
              _lastDrawnSecond(static_cast<unsigned long>(-1)),
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
                    EspNowMidiLog::e("Display init failed");
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
            EspNowMidiLog::e("enomik::Dongle requires a USB-capable chip (ESP32-S2/S3)");
            return false;
#else
            EspNowMidiLog::i("=== ESP-NOW MIDI DONGLE ===");
            EspNowMidiLog::i("ESP-IDF Version: %s", esp_get_idf_version());
            EspNowMidiLog::i("Channel: %d", ESP_NOW_MIDI_CHANNEL);

            TinyUSBDevice.setManufacturerDescriptor(_manufacturer);
            TinyUSBDevice.setProductDescriptor(_product);

            g_dongle_usb_midi.begin();

            if (TinyUSBDevice.mounted())
            {
                TinyUSBDevice.detach();
                delay(100);
            }
            TinyUSBDevice.attach();

            if (!espnowMIDI.begin(loadPowerSavePreference()))
            {
                EspNowMidiLog::e("Failed to initialize ESP-NOW MIDI");
                return false;
            }

            readMacAddress();
            EspNowMidiLog::i("Mac: %s", macToString(_baseMac).c_str());

            loadUsbHealthPrevious();

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

            if (!peerStorage.begin())
            {
                EspNowMidiLog::e("Failed to initialize peer storage");
            }
            else
            {
                EspNowMidiLog::i("Restoring peers from storage...");
                int restoredCount = 0;
                int skippedCount = 0;
                for (int i = 0; i < peerStorage.count(); i++)
                {
                    const uint8_t *mac = peerStorage.get(i);
                    if (!mac)
                    {
                        continue;
                    }
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
                        skippedCount++;
                    }
                }
                EspNowMidiLog::i("Peer restoration complete: %d restored, %d skipped",
                              restoredCount, skippedCount);
            }

            EspNowMidiLog::i("Registered peers: %d", espnowMIDI.getPeersCount());

            if (_display)
            {
                if (!_display->begin())
                {
                    EspNowMidiLog::e("Display init failed");
                    _display = nullptr;
                }
            }

            _isInitialized = true;
            EspNowMidiLog::i("Setup complete - ready!");
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
                EspNowMidiLog::i("USB disconnected");
                _usbMidiInitialized = false;
                if (_usbWatchdog.isRecovering(now))
                {
                    // Our own re-attach: keep what is waiting for the host.
                    _usbClearDeferred = true;
                }
                else
                {
                    _usbMidiQueue.clear();
                }
            }

            if (!_usbMidiInitialized && TinyUSBDevice.mounted())
            {
                EspNowMidiLog::i("USB mounted - initializing MIDI");

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
                EspNowMidiLog::i("USB MIDI ready!");
            }

            if (_usbMidiInitialized)
            {
                DONGLE_USBMIDI.read();
                drainUsbMidiQueue();
            }

            serviceUsbWatchdog(now);
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

        /**
         * @brief Maximum age (ms) of a message waiting for the USB host.
         *
         * When the host stops reading for a while, messages older than this are
         * dropped instead of being delivered late as a backlog. Release messages
         * (Note Off, pedal off, All Notes Off, Stop, ...) are always delivered and
         * the latest value of each CC / pitch bend / pressure / program is kept.
         * Default `USB_MIDI_STALE_MS` (500). 0 disables dropping.
         */
        void setUsbStaleTimeout(uint32_t maxAgeMs)
        {
            _usbStaleMs = maxAgeMs;
        }

        /** @return Current stale timeout in milliseconds (0 = disabled). */
        uint32_t getUsbStaleTimeout() const
        {
            return _usbStaleMs;
        }

        /** @return Messages to the USB host dropped as stale since boot. */
        uint32_t getUsbStaleDropCount()
        {
            return _usbMidiQueue.staleDropCount();
        }

        /** @return Messages to the USB host dropped because the queue was full. */
        uint32_t getUsbOverflowDropCount()
        {
            return _usbMidiQueue.overflowDropCount();
        }

        /**
         * @brief Sets what the USB watchdog may do (default: Recover).
         *
         * The watchdog notices when the computer stops taking MIDI from the
         * dongle while messages are waiting, and recovers by re-attaching USB,
         * like unplugging and replugging the cable:
         * - USB suspended and the computer refuses remote wakeup (at most once
         *   per suspend; see UsbWatchdogConfig::recoverFromSuspend), or
         * - the MIDI IN endpoint not read for 500 ms, but only if the computer
         *   did read from the dongle earlier in this connection and is not
         *   sending MIDI itself (a computer with no app listening is left alone).
         * It backs off (30 s doubling to 10 min) when a re-attach did not get
         * data through. `Observe` only counts, `Off` disables re-attaching.
         */
        void setUsbWatchdogMode(UsbWatchdogMode mode)
        {
            _usbWatchdog.setMode(mode);
        }

        /** @return Current USB watchdog mode. */
        UsbWatchdogMode getUsbWatchdogMode() const
        {
            return _usbWatchdog.mode();
        }

        /** @brief Timing of the USB watchdog; adjust before or after begin(). */
        UsbWatchdogConfig &usbWatchdogConfig()
        {
            return _usbWatchdog.config();
        }

        /** @return USB health counters since boot. */
        const UsbHealthStats &getUsbHealthStats() const
        {
            return _usbWatchdog.stats();
        }

        /**
         * @brief USB health counters of the last earlier session that had a USB
         * incident (refused wakeup or re-attach), saved to flash.
         * @return false when none was saved.
         */
        bool getUsbHealthStatsPrevious(UsbHealthStats &out) const
        {
            if (!_usbPrevStatsValid)
            {
                return false;
            }
            out = _usbPrevStats;
            return true;
        }

        /** @return Value that changes whenever a USB health counter changes. */
        uint32_t usbHealthSignature() const
        {
            const UsbHealthStats &st = _usbWatchdog.stats();
            return st.suspends * 1u + st.longestSuspendMs / 1000u * 3u + st.wakeupsTried * 5u +
                   st.wakeupsRefused * 7u + st.stalls * 11u + st.longestBusyMs * 13u +
                   st.reattaches * 17u + st.recoveries * 19u + static_cast<uint32_t>(_usbWatchdog.mode()) * 23u;
        }

#ifdef ENOMIK_USB_FAULT_INJECT
        /** @brief Test-only USB faults (build with ENOMIK_USB_FAULT_INJECT). */
        enum class UsbFault : uint8_t
        {
            None,
            StuckEndpoint, ///< Pretend the host stopped reading the MIDI IN endpoint.
            Suspended,     ///< Pretend USB is suspended and remote wakeup is refused.
        };

        /** @brief Simulates a USB fault; a watchdog re-attach clears it, like a real replug. */
        void injectUsbFault(UsbFault fault)
        {
            _usbFault = fault;
        }

        UsbFault getUsbFault() const
        {
            return _usbFault;
        }
#endif

        /** @return true when USB is mounted and MIDI handlers are registered. */
        bool isUsbReady() const
        {
            return _usbMidiInitialized;
        }

        /** @return Local STA MAC as a colon-separated hex string. */
        PortableString getMacAddress() const
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

        /**
         * @brief Gets the MAC address of a registered peer.
         * @param index Peer index in `[0, getPeersCount())`.
         * @return Pointer to the 6-byte MAC, or `nullptr` when @p index is out of range.
         */
        const uint8_t *getPeer(int index) const
        {
            return espnowMIDI.getPeer(index);
        }

        /** @brief Force the next loop() to refresh the display (e.g. after UI input). */
        void invalidateDisplay()
        {
            _displayDirty = true;
            _lastDisplayUpdate = 0;
        }

        bool addPeer(const uint8_t mac[6])
        {
            if (!mac)
            {
                return false;
            }
            if (!espnowMIDI.hasPeer(mac))
            {
                if (!espnowMIDI.addPeer(mac))
                {
                    return false;
                }
            }
            if (!peerStorage.exists(mac))
            {
                if (!peerStorage.add(mac))
                {
                    return false;
                }
            }
            return true;
        }

        bool removePeer(const uint8_t mac[6])
        {
            if (!mac)
            {
                return false;
            }
            clearMuted(mac);
            const bool live = espnowMIDI.removePeer(mac);
            bool stored = false;
            if (peerStorage.exists(mac))
            {
                stored = peerStorage.remove(mac);
            }
            return live || stored;
        }

        bool removePeer(int index)
        {
            const uint8_t *mac = espnowMIDI.getPeer(index);
            if (!mac)
            {
                return false;
            }
            uint8_t copy[6];
            memcpy(copy, mac, 6);
            return removePeer(copy);
        }

        /**
         * @brief Replaces a peer MAC in live ESP-NOW + storage.
         *
         * Same address is a no-op. Refuses if @p newMac is already another
         * peer. Session mute follows the new address.
         */
        bool replacePeer(const uint8_t oldMac[6], const uint8_t newMac[6])
        {
            if (!oldMac || !newMac)
            {
                return false;
            }
            if (memcmp(oldMac, newMac, 6) == 0)
            {
                return true;
            }
            const bool oldLive = espnowMIDI.hasPeer(oldMac);
            const bool oldStored = peerStorage.exists(oldMac);
            if (!oldLive && !oldStored)
            {
                return false;
            }
            if (espnowMIDI.hasPeer(newMac) || peerStorage.exists(newMac))
            {
                return false;
            }

            uint8_t oldCopy[6];
            uint8_t newCopy[6];
            memcpy(oldCopy, oldMac, 6);
            memcpy(newCopy, newMac, 6);
            const bool muted = isMuted(oldCopy);

            if (!removePeer(oldCopy))
            {
                return false;
            }
            if (!addPeer(newCopy))
            {
                addPeer(oldCopy);
                if (muted)
                {
                    setMuted(oldCopy, true);
                }
                return false;
            }
            if (muted)
            {
                setMuted(newCopy, true);
            }
            return true;
        }

        /**
         * @brief Mutes or unmutes a registered peer for this session.
         *
         * Muted peers are ignored in both directions (USB ↔ ESP-NOW). Mute is
         * not persisted; deleting a peer also clears its mute.
         */
        bool setMuted(const uint8_t mac[6], bool muted)
        {
            if (!mac || !espnowMIDI.hasPeer(mac))
            {
                return false;
            }
            if (!_mutes.set(mac, muted))
            {
                return false;
            }
            _displayDirty = true;
            return true;
        }

        bool setMuted(int index, bool muted)
        {
            return setMuted(espnowMIDI.getPeer(index), muted);
        }

        bool isMuted(const uint8_t mac[6]) const
        {
            return _mutes.contains(mac);
        }

        bool isMuted(int index) const
        {
            return isMuted(espnowMIDI.getPeer(index));
        }

        /**
         * @brief Power-save preference (modem sleep + lower TX power).
         *
         * Off by default for lower latency. Stored in NVS/Preferences and
         * restored on boot.
         */
        void setPowerSave(bool enabled)
        {
            espnowMIDI.setReducePowerAtCostOfLatency(enabled);
            savePowerSavePreference(enabled);
            _displayDirty = true;
        }

        bool isPowerSave() const
        {
            return espnowMIDI.getReducePowerAtCostOfLatency();
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

        // --- Inject MIDI (ESP-NOW + USB when ready) ---

        bool sendNoteOn(byte note, byte velocity, byte channel)
        {
            midi_message msg;
            msg.status = MIDI_NOTE_ON;
            msg.channel = channel;
            msg.firstByte = note;
            msg.secondByte = velocity;
            queueToUsb(msg, true);
            return sendMidiToUnmutedPeers(msg) == ESP_OK;
        }

        bool sendNoteOff(byte note, byte velocity, byte channel)
        {
            midi_message msg;
            msg.status = MIDI_NOTE_OFF;
            msg.channel = channel;
            msg.firstByte = note;
            msg.secondByte = velocity;
            queueToUsb(msg, true);
            return sendMidiToUnmutedPeers(msg) == ESP_OK;
        }

        bool sendControlChange(byte control, byte value, byte channel)
        {
            midi_message msg;
            msg.status = MIDI_CONTROL_CHANGE;
            msg.channel = channel;
            msg.firstByte = control;
            msg.secondByte = value;
            queueToUsb(msg, true);
            return sendMidiToUnmutedPeers(msg) == ESP_OK;
        }

        bool sendProgramChange(byte program, byte channel)
        {
            midi_message msg;
            msg.status = MIDI_PROGRAM_CHANGE;
            msg.channel = channel;
            msg.firstByte = program;
            msg.secondByte = 0;
            queueToUsb(msg, true);
            return sendMidiToUnmutedPeers(msg) == ESP_OK;
        }

        bool sendAfterTouch(byte pressure, byte channel)
        {
            midi_message msg;
            msg.status = MIDI_AFTERTOUCH;
            msg.channel = channel;
            msg.firstByte = pressure;
            msg.secondByte = 0;
            queueToUsb(msg, true);
            return sendMidiToUnmutedPeers(msg) == ESP_OK;
        }

        bool sendPolyAfterTouch(byte note, byte pressure, byte channel)
        {
            midi_message msg;
            msg.status = MIDI_POLY_AFTERTOUCH;
            msg.channel = channel;
            msg.firstByte = note;
            msg.secondByte = pressure;
            queueToUsb(msg, true);
            return sendMidiToUnmutedPeers(msg) == ESP_OK;
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
            return sendMidiToUnmutedPeers(msg) == ESP_OK;
        }

        bool sendStart()
        {
            midi_message msg;
            msg.status = MIDI_START;
            msg.channel = 0;
            msg.firstByte = 0;
            msg.secondByte = 0;
            queueToUsb(msg, true);
            return sendMidiToUnmutedPeers(msg) == ESP_OK;
        }

        bool sendStop()
        {
            midi_message msg;
            msg.status = MIDI_STOP;
            msg.channel = 0;
            msg.firstByte = 0;
            msg.secondByte = 0;
            queueToUsb(msg, true);
            return sendMidiToUnmutedPeers(msg) == ESP_OK;
        }

        bool sendContinue()
        {
            midi_message msg;
            msg.status = MIDI_CONTINUE;
            msg.channel = 0;
            msg.firstByte = 0;
            msg.secondByte = 0;
            queueToUsb(msg, true);
            return sendMidiToUnmutedPeers(msg) == ESP_OK;
        }

        bool sendClock()
        {
            _usbMidiQueue.enqueueClock();
            midi_message msg;
            msg.status = MIDI_TIME_CLOCK;
            msg.channel = 0;
            msg.firstByte = 0;
            msg.secondByte = 0;
            return sendMidiToUnmutedPeers(msg) == ESP_OK;
        }

        bool sendSongPosition(uint16_t value)
        {
            midi_message msg;
            msg.status = MIDI_SONG_POS_POINTER;
            msg.channel = 0;
            msg.firstByte = value & 0x7F;
            msg.secondByte = (value >> 7) & 0x7F;
            queueToUsb(msg, false);
            return sendMidiToUnmutedPeers(msg) == ESP_OK;
        }

        bool sendSongSelect(uint8_t value)
        {
            midi_message msg;
            msg.status = MIDI_SONG_SELECT;
            msg.channel = 0;
            msg.firstByte = value;
            msg.secondByte = 0;
            queueToUsb(msg, true);
            return sendMidiToUnmutedPeers(msg) == ESP_OK;
        }

    private:
        bool _isInitialized;
        bool _usbMidiInitialized;
        Display *_display;
        uint32_t _lastDisplayUpdate;
        uint32_t _displayIntervalMs;
        bool _displayDirty;
        int _lastDrawnPeerCount;
        char _lastDrawnUsbStatus;
        unsigned long _lastDrawnSecond;
        UsbMidiQueue _usbMidiQueue;
        uint32_t _usbStaleMs = USB_MIDI_STALE_MS;
        uint32_t _lastStaleCheckMs = 0;
        UsbStallWatchdog _usbWatchdog;
        bool _usbAgingPaused = false;
        bool _usbClearDeferred = false;
        bool _usbWrote = false;
        bool _usbLastWriteFailed = false;
        bool _usbEpLookupDone = false;
        uint8_t _usbMidiInEp = 0;
        bool _usbDetachedByWatchdog = false;
        uint32_t _usbDetachedAt = 0;
        UsbHealthStats _usbPrevStats;
        bool _usbPrevStatsValid = false;
        uint32_t _usbSavedIncidents = 0;
        bool _usbStatsSavedOnce = false;
        uint32_t _usbStatsSavedAt = 0;
#ifdef ENOMIK_USB_FAULT_INJECT
        UsbFault _usbFault = UsbFault::None;
#endif
        MidiMessageHistory _messageHistory[DONGLE_MAX_HISTORY];
        int _messageIndex;
        uint8_t _baseMac[6];
        const char *_manufacturer;
        const char *_product;
        PortableString _version;
        BridgeFilter _toHostFilter;
        BridgeFilter _fromHostFilter;
        PeerStorage peerStorage;
        PeerMuteList _mutes;

        void addToHistory(const midi_message &msg, bool outgoing)
        {
            _messageHistory[_messageIndex].message = msg;
            _messageHistory[_messageIndex].outgoing = outgoing;
            _messageHistory[_messageIndex].timestamp = millis();
            _messageIndex = (_messageIndex + 1) % DONGLE_MAX_HISTORY;
            _displayDirty = true;
        }

        void queueToUsb(const midi_message &msg, bool addHistory)
        {
            if (addHistory)
            {
                addToHistory(msg, false);
            }
            _usbMidiQueue.enqueue(msg);
        }

        /** ESP-NOW → USB host. Drops muted senders, then toHost filter, then queues. */
        void bridgeToHost(midi_message &msg, bool addHistory = true)
        {
            const uint8_t *from = espnowMIDI.lastSenderMac();
            if (from && isMuted(from))
            {
                return;
            }
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
            _usbWatchdog.noteHostRx(millis());
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
            sendMidiToUnmutedPeers(msg);
        }

        void clearMuted(const uint8_t mac[6])
        {
            _mutes.clear(mac);
            _displayDirty = true;
        }

        esp_err_t sendToUnmutedPeers(const uint8_t *data, size_t len)
        {
            const int n = espnowMIDI.getPeersCount();
            if (n == 0)
            {
                return ESP_FAIL;
            }
            esp_err_t result = ESP_OK;
            for (int i = 0; i < n; i++)
            {
                const uint8_t *mac = espnowMIDI.getPeer(i);
                if (!mac || isMuted(mac))
                {
                    continue;
                }
                const esp_err_t err = espnowMIDI.send(mac, data, len);
                if (err != ESP_OK)
                {
                    result = err;
                }
            }
            return result;
        }

        esp_err_t sendMidiToUnmutedPeers(const midi_message &msg)
        {
            return sendToUnmutedPeers(reinterpret_cast<const uint8_t *>(&msg), sizeof(msg));
        }

        void readMacAddress()
        {
            esp_err_t ret = esp_wifi_get_mac(WIFI_IF_STA, _baseMac);
            if (ret != ESP_OK)
            {
                EspNowMidiLog::e("Failed to read MAC address");
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

            const bool written = g_dongle_usb_midi.writePacket(packet);
            if (written)
            {
                _usbWrote = true;
            }
            return written;
        }

        void dropStaleUsbMidi()
        {
            const uint32_t now = millis();
            // While the watchdog re-attaches USB, waiting messages must not age
            // out; afterwards they get a fresh stale window.
            if (_usbWatchdog.isRecovering(now))
            {
                _usbAgingPaused = true;
                return;
            }
            if (_usbAgingPaused)
            {
                _usbAgingPaused = false;
                _usbMidiQueue.refreshTimestamps(now);
            }
            if (_usbStaleMs == 0)
            {
                return;
            }
            // Rate-limited: a stalled queue would otherwise be rescanned every loop.
            if ((uint32_t)(now - _lastStaleCheckMs) < 20)
            {
                return;
            }
            _lastStaleCheckMs = now;
            const uint16_t dropped = _usbMidiQueue.dropStale(now, _usbStaleMs);
            if (dropped > 0)
            {
                EspNowMidiLog::d("USB host not reading: dropped %u stale message(s)", dropped);
            }
        }

        void drainUsbMidiQueue()
        {
            if (!TinyUSBDevice.mounted())
            {
                return;
            }

            dropStaleUsbMidi();

            // Remote wakeup while suspended is requested by the USB watchdog
            // (rate-limited) instead of on every loop.
            if (TinyUSBDevice.suspended())
            {
                return;
            }

            if (!TinyUSBDevice.ready())
            {
                return;
            }

#ifdef ENOMIK_USB_FAULT_INJECT
            if (_usbFault != UsbFault::None)
            {
                return; // simulated: nothing reaches the host
            }
#endif

            midi_message msg;
            while (_usbMidiQueue.peek(msg))
            {
                if (!sendQueuedMidi(msg))
                {
                    _usbLastWriteFailed = true;
                    return;
                }
                _usbMidiQueue.consumeHead();
            }
            _usbLastWriteFailed = false;
        }

        /** @return true while data handed to USB has not been read by the host. */
        bool usbTxBlocked()
        {
#ifdef ENOMIK_USB_FAULT_INJECT
            if (_usbFault == UsbFault::StuckEndpoint)
            {
                return _usbMidiQueue.hasPending();
            }
#endif
            if (!_usbEpLookupDone && TinyUSBDevice.mounted())
            {
                _usbEpLookupDone = true;
                if (tud_descriptor_configuration_cb && usbd_edpt_busy)
                {
                    _usbMidiInEp = findMidiInEndpoint(tud_descriptor_configuration_cb(0));
                }
                if (_usbMidiInEp)
                {
                    EspNowMidiLog::i("USB watchdog: MIDI IN endpoint 0x%02X", _usbMidiInEp);
                }
                else
                {
                    EspNowMidiLog::w("USB watchdog: MIDI IN endpoint unknown, using queue fallback");
                }
            }
            if (_usbMidiInEp && usbd_edpt_busy)
            {
                return usbd_edpt_busy(0, _usbMidiInEp);
            }
            // Fallback: only a full TinyUSB buffer is visible.
            return _usbMidiQueue.hasPending() && _usbLastWriteFailed;
        }

        void serviceUsbWatchdog(uint32_t now)
        {
            UsbWatchdogInputs in;
            in.now = now;
            in.mounted = TinyUSBDevice.mounted();
            in.suspended = TinyUSBDevice.suspended();
            in.pending = _usbMidiQueue.hasPending();
            in.txBlocked = usbTxBlocked();
            in.endpointKnown = _usbMidiInEp != 0 && usbd_edpt_busy != nullptr;
            in.wrote = _usbWrote;
            _usbWrote = false;
#ifdef ENOMIK_USB_FAULT_INJECT
            if (_usbFault == UsbFault::Suspended && in.mounted)
            {
                in.suspended = true;
            }
            if (_usbFault == UsbFault::StuckEndpoint)
            {
                in.endpointKnown = true;
            }
#endif

            switch (_usbWatchdog.tick(in))
            {
            case UsbWatchdogAction::RemoteWakeup:
            {
                bool accepted = false;
#ifdef ENOMIK_USB_FAULT_INJECT
                if (_usbFault == UsbFault::Suspended)
                {
                    _usbWatchdog.noteWakeupResult(false);
                    break;
                }
#endif
                accepted = tud_remote_wakeup();
                // false can also mean the host resumed the bus just now: only
                // a refusal while still suspended counts.
                _usbWatchdog.noteWakeupResult(accepted || !TinyUSBDevice.suspended());
                EspNowMidiLog::i("USB suspended with MIDI waiting: remote wakeup %s",
                                 accepted ? "sent" : "not allowed by host");
                break;
            }
            case UsbWatchdogAction::Detach:
                EspNowMidiLog::w("USB host stopped taking MIDI: re-attaching USB");
                TinyUSBDevice.detach();
                _usbDetachedByWatchdog = true;
                _usbDetachedAt = now;
#ifdef ENOMIK_USB_FAULT_INJECT
                _usbFault = UsbFault::None; // a replug fixes the simulated fault
#endif
                break;
            case UsbWatchdogAction::Attach:
                TinyUSBDevice.attach();
                _usbDetachedByWatchdog = false;
                EspNowMidiLog::i("USB re-attached");
                break;
            default:
                break;
            }

            // Failsafe: never stay detached, whatever happens above.
            if (_usbDetachedByWatchdog && (uint32_t)(now - _usbDetachedAt) > 2000)
            {
                TinyUSBDevice.attach();
                _usbDetachedByWatchdog = false;
                EspNowMidiLog::e("USB watchdog: forced re-attach");
            }

            // A disconnect during our recovery kept the queue; if USB did not
            // come back by the end of the recovery window, clear it as usual.
            if (_usbClearDeferred && !_usbWatchdog.isRecovering(now))
            {
                _usbClearDeferred = false;
                if (!TinyUSBDevice.mounted())
                {
                    _usbMidiQueue.clear();
                }
            }

            saveUsbHealthIfNeeded(now);
        }

        static constexpr uint32_t kUsbStatsMagic = 0x55534231; // "USB1"

        struct StoredUsbHealth
        {
            uint32_t magic;
            UsbHealthStats stats;
        };

        // Stalls alone are not saved: a host with no app reading the input
        // stalls on every message without anything being wrong.
        static uint32_t usbIncidents(const UsbHealthStats &st)
        {
            return st.wakeupsRefused + st.reattaches + st.recoveries;
        }

        void loadUsbHealthPrevious()
        {
            Preferences prefs;
            if (!prefs.begin("enomik", true))
            {
                return;
            }
            StoredUsbHealth stored{};
            const size_t n = prefs.getBytes("usb_health", &stored, sizeof(stored));
            prefs.end();
            if (n == sizeof(stored) && stored.magic == kUsbStatsMagic)
            {
                _usbPrevStats = stored.stats;
                _usbPrevStatsValid = true;
            }
        }

        // Writes only after a USB incident, at most once a minute, and not
        // during a recovery (re-enumeration): normal operation causes no flash
        // writes.
        void saveUsbHealthIfNeeded(uint32_t now)
        {
            const UsbHealthStats &st = _usbWatchdog.stats();
            const uint32_t incidents = usbIncidents(st);
            if (incidents == _usbSavedIncidents || _usbWatchdog.isRecovering(now))
            {
                return;
            }
            if (_usbStatsSavedOnce && (uint32_t)(now - _usbStatsSavedAt) < 60000)
            {
                return;
            }
            StoredUsbHealth stored{};
            stored.magic = kUsbStatsMagic;
            stored.stats = st;
            Preferences prefs;
            if (prefs.begin("enomik", false))
            {
                prefs.putBytes("usb_health", &stored, sizeof(stored));
                prefs.end();
            }
            _usbSavedIncidents = incidents;
            _usbStatsSavedOnce = true;
            _usbStatsSavedAt = now;
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
            EspNowMidiLog::d("USB status=%c mounted=%d suspended=%d ready=%d queue=%u",
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

            const int peerCount = espnowMIDI.getPeersCount();
            const char usbStatus = getUsbStatusChar();
            const unsigned long second = now / 1000;

            // Header text (uptime, con/usb toggle) only changes once a second, so a
            // second boundary forces a redraw even without new history/peer/usb
            // activity. Otherwise skip the whole clear+draw+flush when nothing the
            // display shows has actually changed since the last frame.
            if (!_displayDirty && peerCount == _lastDrawnPeerCount &&
                usbStatus == _lastDrawnUsbStatus && second == _lastDrawnSecond)
            {
                return;
            }

            _displayDirty = false;
            _lastDrawnPeerCount = peerCount;
            _lastDrawnUsbStatus = usbStatus;
            _lastDrawnSecond = second;

            _display->update(
                _baseMac,
                _version.c_str(),
                peerCount,
                usbStatus,
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
