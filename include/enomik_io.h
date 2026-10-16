#pragma once

#include <cstdio>
#include <vector>
#include "./esp_now_midi.h"
#include "../utils/mac.h"
#include "./enomik_sysex.h"
#include "./enomik_pinconfig.h"
#include "./esp_now_midi_prefs.h"
#include "./esp_now_midi_compat.h"
#include "./esp_now_midi_gpio.h"

// Firmware identity, used to reset the stored client name after a new flash
#if __has_include("esp_app_desc.h")
#include "esp_app_desc.h"
#define ENOMIK_HAS_APP_DESC 1
#elif __has_include("esp_ota_ops.h")
#include "esp_ota_ops.h"
#define ENOMIK_HAS_OTA_APP_DESC 1
#endif

// Detect ESP32 variant and set ADC resolution
#if defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S3)
#define ADC_RESOLUTION 13
#define ADC_MAX_VALUE 8191
#define TOUCH_MAX_VALUE 100
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
#define ADC_RESOLUTION 12
#define ADC_MAX_VALUE 4095
#define TOUCH_MAX_VALUE 100
#else // Original ESP32
#define ADC_RESOLUTION 12
#define ADC_MAX_VALUE 4095
#define TOUCH_MAX_VALUE 100
#endif

// analogWrite() on ESP32 uses 8-bit PWM (0-255) by default. Using ADC_MAX_VALUE
// would map most MIDI values above ~4 to 255, making vel/CC 60 behave like 127.
#ifndef PWM_MAX_VALUE
#define PWM_MAX_VALUE 255
#endif

namespace enomik
{
    // Pin mode constants for clarity
    static constexpr uint8_t ENOMIK_INPUT = 0x00;
    static constexpr uint8_t ENOMIK_OUTPUT = 0x01;
    static constexpr uint8_t ENOMIK_INPUT_PULLUP = 0x02;
    static constexpr uint8_t ENOMIK_ANALOG_INPUT = 0x03;
    static constexpr uint8_t ENOMIK_ANALOG_OUTPUT = 0x04;
    static constexpr uint8_t ENOMIK_INPUT_TOUCH = 0x05;

    struct PinState
    {
        int lastValue = -1;
        /** Last raw digital reading, used to time debounce stability. */
        int lastRawValue = -1;
        unsigned long lastChangeTime = 0;
        unsigned long lastSendTime = 0;
        float smoothedValue = 0;
        uint8_t spikeCount = 0;
        bool touched = false;
    };

    /**
     * @brief Configurable GPIO-to-MIDI bridge with SysEx configuration support.
     *
     * Pin mappings are restored from NVS in begin(). Call loop() regularly to
     * poll configured input pins and emit MIDI through the registered callback.
     */
    class IO
    {
    public:
        static constexpr unsigned long DEBOUNCE_MS = 4;
        static constexpr unsigned long ANALOG_MIN_INTERVAL = 5;
        /** EMA alpha on raw ADC/touch; lower = less chatter after 7-bit quantize. */
        static constexpr float SMOOTHING_FACTOR = 0.15f;
        /** Snap mapped 7-bit values this close to min/max onto the endpoint. */
        static constexpr int ANALOG_ENDPOINT_SNAP = 1;
        /** Snap window for 14-bit pitch bend endpoints. */
        static constexpr int PITCH_BEND_ENDPOINT_SNAP = 64;
        /** Ignore ADC jumps larger than this until confirmed (pot wiper dropouts). */
        static constexpr int ANALOG_SPIKE_LIMIT = ADC_MAX_VALUE / 4;
        /** Consecutive out-of-range samples required before accepting a large jump. */
        static constexpr uint8_t ANALOG_SPIKE_CONFIRM = 3;

        /** @brief Restores pin configurations, initializes their hardware, and configures SysEx handlers. */
        void begin()
        {
            analogReadResolution(ADC_RESOLUTION);
            _pinConfigs = loadPinConfigsFromPrefs();
            _midiLoopback = loadMidiLoopbackFromPrefs();
            _powerSave = loadPowerSaveFromPrefs();
            loadNameFromPrefs();
            _pinStates.clear();

            for (const auto &config : _pinConfigs)
            {
                initializePinHardware(config);
                _pinStates.push_back(PinState());
            }

            setupSysExHandlers();
        }

        /** @brief Returns whether outgoing Client MIDI is locally looped back to receive handlers. */
        bool isMidiLoopback() const { return _midiLoopback; }

        /** @brief Enables or disables MIDI loopback and persists the setting. */
        void setMidiLoopback(bool enabled)
        {
            _midiLoopback = enabled;
            saveMidiLoopbackToPrefs(_midiLoopback);
        }

        /** @brief Returns whether power-save mode is enabled (persisted preference). */
        bool isPowerSave() const { return _powerSave; }

        /** @brief Enables or disables power-save preference, persists it, and notifies listeners. */
        void setPowerSave(bool enabled)
        {
            _powerSave = enabled;
            savePowerSaveToPrefs(_powerSave);
            if (_onPowerSaveChanged)
            {
                _onPowerSaveChanged(_powerSave);
            }
        }

        /**
         * @brief Sets the sketch's default client name (used when the app has not set one).
         *
         * Printable ASCII only, at most MAX_NAME_LENGTH characters; longer names are cut,
         * other characters are replaced with '?'.
         */
        void setDefaultName(const char *name)
        {
            copyName(_defaultName, name);
            if (_defaultName[0] == '\0')
            {
                copyName(_defaultName, DEFAULT_NAME);
            }
        }

        /** @brief Returns the effective client name: the app override if set, else the sketch default. */
        const char *getName() const
        {
            return _nameOverride[0] != '\0' ? _nameOverride : _defaultName;
        }

        /** @brief Returns the name set in the sketch (setName()), ignoring the app override. */
        const char *getDefaultName() const
        {
            return _defaultName;
        }

        /** @brief Sets (or with an empty name clears) the app name override and persists it. */
        void setNameOverride(const char *name)
        {
            copyName(_nameOverride, name);
            saveNameToPrefs();
        }

        /**
         * @brief Replaces analogRead() for analog input pins, e.g. to fake a sensor in tests.
         * @param reader Returns the raw ADC value (`0`..`ADC_MAX_VALUE`) for a pin; `nullptr` restores analogRead().
         */
        void setAnalogReader(std::function<int(uint8_t pin)> reader)
        {
            _analogReader = reader;
        }

        /**
         * @brief Replaces digitalRead() for digital input pins, e.g. to fake buttons in tests.
         * @param reader Returns `HIGH` or `LOW` for a pin; `nullptr` restores digitalRead().
         */
        void setDigitalReader(std::function<int(uint8_t pin)> reader)
        {
            _digitalReader = reader;
        }

        /** @brief Registers a callback invoked when the power-save preference changes. */
        void setOnPowerSaveChanged(std::function<void(bool)> callback)
        {
            _onPowerSaveChanged = callback;
        }

        /** @brief Polls configured input pins and sends MIDI for relevant changes. */
        void loop()
        {
            unsigned long now = millis();

            for (size_t i = 0; i < _pinConfigs.size(); i++)
            {
                auto &config = _pinConfigs[i];
                auto &state = _pinStates[i];

                if (config.mode == ENOMIK_OUTPUT || config.mode == ENOMIK_ANALOG_OUTPUT)
                    continue;

                processPinInput(config, state, now);
            }
        }

        /** @brief Applies a received Note On message to matching output pins. */
        void onNoteOn(byte channel, byte note, byte velocity)
        {
            for (auto &config : _pinConfigs)
            {
                if (config.midi_type == MidiStatus::MIDI_NOTE_ON &&
                    config.midi_channel == channel &&
                    config.midi_note == note)
                {
                    // MIDI: Note On with velocity 0 is equivalent to Note Off
                    if (velocity == 0)
                    {
                        onNoteOff(channel, note, 0);
                        return;
                    }
                    // digital pins switch fully on regardless of velocity
                    writeOutput(config, config.mode == ENOMIK_OUTPUT ? 127 : velocity);
                }
            }
        }

        /** @brief Applies a received Note Off message to matching output pins. */
        void onNoteOff(byte channel, byte note, byte velocity)
        {
            for (auto &config : _pinConfigs)
            {
                if ((
                        config.midi_type == MidiStatus::MIDI_NOTE_OFF ||
                        config.midi_type == MidiStatus::MIDI_NOTE_ON) &&
                    config.midi_channel == channel &&
                    config.midi_note == note)
                {
                    writeOutput(config, 0);
                }
            }
        }

        /** @brief Applies signed pitch bend (`-8192`–`8191`, center `0`) to matching output pins. */
        void onPitchBend(byte channel, int bend)
        {
            for (auto &config : _pinConfigs)
            {
                if (config.midi_type == MidiStatus::MIDI_PITCH_BEND &&
                    config.midi_channel == channel)
                {
                    writePitchBend(config, bend);
                }
            }
        }

        /** @brief Applies a received Control Change message to matching output pins. */
        void onControlChange(byte channel, byte control, byte value)
        {
            for (auto &config : _pinConfigs)
            {
                if (config.midi_type == MidiStatus::MIDI_CONTROL_CHANGE &&
                    config.midi_channel == channel &&
                    config.midi_cc == control)
                {
                    writeOutput(config, value);
                }
            }
        }

        /** @brief Receives Program Change; currently reserved for future use. */
        void onProgramChange(byte channel, byte program)
        {
            // Reserved for future use
        }

        /** @brief Passes an incoming SysEx message to the configuration handler. */
        void onSysEx(const uint8_t *data, uint16_t length)
        {
            _sysexHandler.handleSysEx(data, length);
        }

        /** @brief Registers the callback used to send MIDI produced by pin inputs. */
        void setOnMIDISendRequest(std::function<void(midi_message)> callback)
        {
            _onMIDISendRequest = callback;
        }

        /** @brief Registers the SysEx add-peer callback. */
        void setOnAddPeerRequest(std::function<AddPeerResult(uint8_t mac[])> callback)
        {
            _onAddPeerRequest = callback;
        }

        /** @brief Registers the SysEx get-peer-by-index callback. */
        void setOnGetPeerRequest(std::function<const uint8_t *(uint8_t index)> callback)
        {
            _onGetPeerRequest = callback;
        }

        /** @brief Registers the callback invoked after a SysEx reset. */
        void setOnResetRequest(std::function<void()> callback)
        {
            _onResetRequest = callback;
        }
        /** @brief Registers the callback used for outgoing SysEx responses. */
        void setOnSysExSendRequest(std::function<void(midi_sysex_message)> callback)
        {
            _onSysExSendRequest = callback;
        }

        /** @brief Logs all loaded pin configurations. */
        void printPinConfigs()
        {
            EspNowMidiLog::i("=== Pin Configurations ===");
            for (size_t i = 0; i < _pinConfigs.size(); i++)
            {
                const auto &cfg = _pinConfigs[i];
                EspNowMidiLog::i(
                    "Pin: %u | Mode: %u | MIDI Channel: %u | MIDI Type: %u | CC: %u | Note: %u | Min MIDI: %u | Max MIDI: %u",
                    cfg.pin, cfg.mode, cfg.midi_channel, static_cast<uint8_t>(cfg.midi_type),
                    cfg.midi_cc, cfg.midi_note, cfg.min_midi_value, cfg.max_midi_value);
            }
            EspNowMidiLog::i("==========================");
        }

    private:
        std::vector<PinConfig> _pinConfigs;
        std::vector<PinState> _pinStates;
        SysExHandler _sysexHandler;
        Preferences _preferences;
        bool _midiLoopback = false;
        bool _powerSave = false;
        static constexpr const char *DEFAULT_NAME = "enomik";
        static constexpr size_t FIRMWARE_ID_LENGTH = 8;
        char _defaultName[MAX_NAME_LENGTH + 1] = "enomik";
        char _nameOverride[MAX_NAME_LENGTH + 1] = "";

        // External callbacks
        std::function<void(midi_message)> _onMIDISendRequest;
        std::function<AddPeerResult(uint8_t mac[])> _onAddPeerRequest;
        std::function<const uint8_t *(uint8_t index)> _onGetPeerRequest;
        std::function<void()> _onResetRequest;
        std::function<void(bool)> _onPowerSaveChanged;
        std::function<int(uint8_t)> _analogReader;
        std::function<int(uint8_t)> _digitalReader;

        void setupSysExHandlers()
        {
            // Handler for setting pin configuration
            _sysexHandler.setOnSetPinConfig([this](const PinConfig &cfg)
                                            {
                EspNowMidiLog::d("SysEx: Setting pin config");
                upsertPinConfig(cfg);
                _sysexHandler.sendPinConfigResponse(cfg, SysExCommand::SET_PIN_CONFIG_RESPONSE); });

            // Handler for getting single pin configuration
            _sysexHandler.setOnGetPinConfig([this](uint8_t pin)
                                            {
                EspNowMidiLog::d("SysEx: Getting config for pin %u", pin);
                for (const auto &cfg : _pinConfigs)
                {
                    if (cfg.pin == pin)
                    {
                        _sysexHandler.sendPinConfigResponse(cfg, SysExCommand::GET_PIN_CONFIG_RESPONSE);
                        return;
                    }
                }
                EspNowMidiLog::w("SysEx: Pin config not found");
                _sysexHandler.sendErrorResponse(
                    static_cast<uint8_t>(SysExCommand::GET_PIN_CONFIG),
                    SysExErrorCode::PIN_NOT_FOUND,
                    pin); });

            // Handler for getting all pin configurations
            _sysexHandler.setOnGetAllPinConfigs([this]()
                                                {
                EspNowMidiLog::d("SysEx: Getting all pin configs");
                for (const auto &cfg : _pinConfigs)
                {
                    _sysexHandler.sendPinConfigResponse(cfg, SysExCommand::GET_ALL_PIN_CONFIGS_RESPONSE);
                }
                _sysexHandler.sendStreamEnd(SysExCommand::GET_ALL_PIN_CONFIGS_RESPONSE); });

            // Handler for deleting pin configuration
            _sysexHandler.setOnDeletePinConfig([this](uint8_t pin)
                                               {
                EspNowMidiLog::d("SysEx: Deleting config for pin %u", pin);
                
                for (size_t i = 0; i < _pinConfigs.size(); i++)
                {
                    if (_pinConfigs[i].pin == pin)
                    {
                        _pinConfigs.erase(_pinConfigs.begin() + i);
                        _pinStates.erase(_pinStates.begin() + i);
                        savePinConfigsToPrefs(_pinConfigs);
                        _sysexHandler.sendDeleteResponse(pin);
                        return;
                    }
                }
                EspNowMidiLog::w("SysEx: Pin config not found for delete");
                _sysexHandler.sendErrorResponse(
                    static_cast<uint8_t>(SysExCommand::DELETE_PIN_CONFIG),
                    SysExErrorCode::PIN_NOT_FOUND,
                    pin); });

            // Handler for clearing all pin configurations
            _sysexHandler.setOnClearPinConfigs([this]()
                                               {
                EspNowMidiLog::d("SysEx: Clearing all pin configs");
                _pinConfigs.clear();
                _pinStates.clear();
                savePinConfigsToPrefs(_pinConfigs);
                _sysexHandler.sendSimpleResponse(SysExCommand::CLEAR_PIN_CONFIGS_RESPONSE); });

            // Handler for getting MAC address
            _sysexHandler.setOnGetMAC([this]()
                                      {
                EspNowMidiLog::d("SysEx: Getting MAC address");
                uint8_t mac[6];
                esp_read_mac(mac, ESP_MAC_WIFI_STA);
                _sysexHandler.sendMACResponse(mac); });

            // Handler for adding peer
            _sysexHandler.setOnAddPeer([this](const uint8_t mac[6]) -> AddPeerResult
                                       {
                EspNowMidiLog::mac("SysEx: Adding peer ", mac);

                if (!_onAddPeerRequest)
                {
                    return AddPeerResult::NotReady;
                }

                uint8_t macCopy[6];
                memcpy(macCopy, mac, 6);
                return _onAddPeerRequest(macCopy);
            });

            // Handler for getting all peers (one response per peer, then stream end)
            _sysexHandler.setOnGetAllPeers([this]()
                                        {
                EspNowMidiLog::d("SysEx: Getting peers list");
                if (_onGetPeerRequest)
                {
                    for (uint8_t index = 0; index < MAX_PEERS; index++)
                    {
                        const uint8_t *mac = _onGetPeerRequest(index);
                        if (!mac)
                            break;
                        _sysexHandler.sendPeerResponse(
                            index, mac, SysExCommand::GET_ALL_PEERS_RESPONSE);
                    }
                }
                _sysexHandler.sendStreamEnd(SysExCommand::GET_ALL_PEERS_RESPONSE); });

            // Handler for getting one peer by storage index
            _sysexHandler.setOnGetPeer([this](uint8_t index)
                                       {
                EspNowMidiLog::d("SysEx: Getting peer at index %u", index);
                const uint8_t *mac = _onGetPeerRequest ? _onGetPeerRequest(index) : nullptr;
                if (mac)
                {
                    _sysexHandler.sendPeerResponse(
                        index, mac, SysExCommand::GET_PEER_RESPONSE);
                }
                else
                {
                    _sysexHandler.sendErrorResponse(
                        static_cast<uint8_t>(SysExCommand::GET_PEER),
                        SysExErrorCode::PEER_NOT_FOUND,
                        index);
                } });

            // Handler for full board config (pins + peers + flags, then GET_CONFIG_RESPONSE)
            _sysexHandler.setOnGetConfig([this]()
                                         {
                EspNowMidiLog::d("SysEx: Streaming full board config");
                for (const auto &cfg : _pinConfigs)
                {
                    _sysexHandler.sendPinConfigResponse(
                        cfg, SysExCommand::GET_ALL_PIN_CONFIGS_RESPONSE);
                }
                if (_onGetPeerRequest)
                {
                    for (uint8_t index = 0; index < MAX_PEERS; index++)
                    {
                        const uint8_t *mac = _onGetPeerRequest(index);
                        if (!mac)
                            break;
                        _sysexHandler.sendPeerResponse(
                            index, mac, SysExCommand::GET_ALL_PEERS_RESPONSE);
                    }
                }
                _sysexHandler.sendMidiLoopbackResponse(
                    SysExCommand::GET_MIDI_LOOPBACK_RESPONSE, _midiLoopback);
                _sysexHandler.sendPowerSaveResponse(
                    SysExCommand::GET_POWER_SAVE_RESPONSE, _powerSave);
                _sysexHandler.sendNameResponse(SysExCommand::GET_NAME_RESPONSE, getName());
                _sysexHandler.sendSimpleResponse(SysExCommand::GET_CONFIG_RESPONSE); });

            _sysexHandler.setOnSetMidiLoopback([this](bool enabled)
                                               {
                EspNowMidiLog::d("SysEx: Setting MIDI loopback %s", enabled ? "on" : "off");
                setMidiLoopback(enabled);
                _sysexHandler.sendMidiLoopbackResponse(
                    SysExCommand::SET_MIDI_LOOPBACK_RESPONSE, _midiLoopback); });

            _sysexHandler.setOnGetMidiLoopback([this]()
                                               {
                EspNowMidiLog::d("SysEx: Getting MIDI loopback");
                _sysexHandler.sendMidiLoopbackResponse(
                    SysExCommand::GET_MIDI_LOOPBACK_RESPONSE, _midiLoopback); });

            _sysexHandler.setOnSetPowerSave([this](bool enabled)
                                            {
                EspNowMidiLog::d("SysEx: Setting power save %s", enabled ? "on" : "off");
                setPowerSave(enabled);
                _sysexHandler.sendPowerSaveResponse(
                    SysExCommand::SET_POWER_SAVE_RESPONSE, _powerSave); });

            _sysexHandler.setOnGetPowerSave([this]()
                                            {
                EspNowMidiLog::d("SysEx: Getting power save");
                _sysexHandler.sendPowerSaveResponse(
                    SysExCommand::GET_POWER_SAVE_RESPONSE, _powerSave); });

            _sysexHandler.setOnSetName([this](const char *name)
                                       {
                EspNowMidiLog::d("SysEx: Setting name '%s'", name);
                setNameOverride(name);
                _sysexHandler.sendNameResponse(SysExCommand::SET_NAME_RESPONSE, getName()); });

            _sysexHandler.setOnGetName([this]()
                                       {
                EspNowMidiLog::d("SysEx: Getting name");
                _sysexHandler.sendNameResponse(SysExCommand::GET_NAME_RESPONSE, getName()); });

            // Handler for system reset
            _sysexHandler.setOnReset([this]()
                                     {
                EspNowMidiLog::i("SysEx: Performing system reset");
                
                // Clear in-memory configurations
                _pinConfigs.clear();
                _pinStates.clear();
                _midiLoopback = false;
                _powerSave = false;
                _nameOverride[0] = '\0';

                // Clear stored preferences
                _preferences.begin("pinconfigs", false);
                _preferences.clear();
                _preferences.end();
                _preferences.begin("enomik", false);
                _preferences.clear();
                _preferences.end();
                saveFirmwareIdToPrefs(); // not a new flash: keep the next boot from resetting again

                if (_onPowerSaveChanged)
                {
                    _onPowerSaveChanged(false);
                }

                if (_onResetRequest)
                {
                    _onResetRequest();
                }

                _sysexHandler.sendSimpleResponse(SysExCommand::RESET_RESPONSE);
                EspNowMidiLog::i("System reset complete"); });

            // Handler for getting protocol version
            _sysexHandler.setOnGetVersion([this]()
                                          {
                EspNowMidiLog::d("SysEx: Getting version");
                _sysexHandler.sendVersionResponse(); });

            // Handler for sending SysEx messages back out
            _sysexHandler.setOnSend([this](const midi_sysex_message &msg)
                                    {
                // Forward to external world if callback is set
                if (_onSysExSendRequest)
                {
                    _onSysExSendRequest(msg);
                } });
        }

        void processPinInput(const PinConfig &config, PinState &state, unsigned long now)
        {
            int currentValue = 0;
            bool shouldSend = false;

            switch (config.mode)
            {
            case ENOMIK_INPUT:
            case ENOMIK_INPUT_PULLUP:
                shouldSend = processDigitalInput(config, state, now, currentValue);
                break;

            case ENOMIK_ANALOG_INPUT:
                shouldSend = processAnalogInput(config, state, now, currentValue);
                break;

            case ENOMIK_INPUT_TOUCH:
                shouldSend = processTouchInput(config, state, now, currentValue);
                break;
            }

            if (shouldSend)
            {
                state.lastValue = currentValue;
                state.lastSendTime = now;
                sendMidiMessage(config, currentValue);
            }
        }

        bool processDigitalInput(const PinConfig &config, PinState &state,
                                 unsigned long now, int &currentValue)
        {
            const int reading = _digitalReader ? _digitalReader(config.pin) : digitalRead(config.pin);

            // Restart the stability window whenever the raw level flips.
            if (reading != state.lastRawValue)
            {
                state.lastRawValue = reading;
                state.lastChangeTime = now;
            }

            // First sample reports the initial state immediately; later
            // changes must stay stable for DEBOUNCE_MS before being sent.
            if (state.lastValue != -1 && now - state.lastChangeTime < DEBOUNCE_MS)
                return false;

            if (reading == state.lastValue)
                return false;

            currentValue = reading;
            return true;
        }

        static int snapToEndpoints(int value, int minValue, int maxValue, int snap)
        {
            if (value <= minValue + snap)
                return minValue;
            if (value >= maxValue - snap)
                return maxValue;
            return value;
        }

        /** EMA update with glitch rejection for intermittent ADC dropouts. */
        static bool updateSmoothedAnalog(int rawValue, PinState &state)
        {
            if (state.lastValue == -1)
            {
                state.smoothedValue = rawValue;
                state.spikeCount = 0;
                return true;
            }

            if (abs(rawValue - (int)state.smoothedValue) > ANALOG_SPIKE_LIMIT)
            {
                state.spikeCount++;
                if (state.spikeCount < ANALOG_SPIKE_CONFIRM)
                    return false; // ignore transient glitch

                // Sustained large move (e.g. fast pot sweep) — accept immediately.
                state.smoothedValue = rawValue;
                state.spikeCount = 0;
                return true;
            }

            state.spikeCount = 0;
            state.smoothedValue = (SMOOTHING_FACTOR * rawValue) +
                                  (1.0f - SMOOTHING_FACTOR) * state.smoothedValue;
            return true;
        }

        bool processAnalogInput(const PinConfig &config, PinState &state,
                                unsigned long now, int &currentValue)
        {
            int rawValue = _analogReader ? _analogReader(config.pin) : analogRead(config.pin);
            if (!updateSmoothedAnalog(rawValue, state))
                return false;

            if (config.midi_type == MidiStatus::MIDI_PITCH_BEND)
            {
                currentValue = map((int)state.smoothedValue, 0, ADC_MAX_VALUE, 0, 16383);
                currentValue = constrain(currentValue, 0, 16383);
                currentValue = snapToEndpoints(currentValue, 0, 16383, PITCH_BEND_ENDPOINT_SNAP);
            }
            else
            {
                const int minMidi = config.min_midi_value;
                const int maxMidi = config.max_midi_value;
                currentValue = map((int)state.smoothedValue, 0, ADC_MAX_VALUE, minMidi, maxMidi);
                currentValue = constrain(currentValue, minMidi, maxMidi);
                currentValue = snapToEndpoints(currentValue, minMidi, maxMidi, ANALOG_ENDPOINT_SNAP);
            }

            if (state.lastValue != -1 && currentValue == state.lastValue)
                return false;

            if (now - state.lastSendTime < ANALOG_MIN_INTERVAL)
                return false;

            return true;
        }

        bool processTouchInput(const PinConfig &config, PinState &state,
                               unsigned long now, int &currentValue)
        {
            int touchValue = touchRead(config.pin);

            if (config.threshold == 0)
            {
                // Continuous mode: smooth, map, snap endpoints, send on change.
                if (state.lastValue == -1)
                    state.smoothedValue = touchValue;
                else
                    state.smoothedValue = (SMOOTHING_FACTOR * touchValue) +
                                          (1.0f - SMOOTHING_FACTOR) * state.smoothedValue;

                // Invert mapping: lower touch value (stronger touch) = higher MIDI value
                int mappedValue = map((int)state.smoothedValue, 0, 100, 127, 0);
                mappedValue = constrain(mappedValue, 0, 127);

                const int minMidi = config.min_midi_value;
                const int maxMidi = config.max_midi_value;
                currentValue = map(mappedValue, 0, 127, minMidi, maxMidi);
                currentValue = constrain(currentValue, minMidi, maxMidi);
                currentValue = snapToEndpoints(currentValue, minMidi, maxMidi, ANALOG_ENDPOINT_SNAP);

                if (state.lastValue != -1 && currentValue == state.lastValue)
                    return false;

                if (now - state.lastSendTime < ANALOG_MIN_INTERVAL)
                    return false;

                return true;
            }
            else
            {
                // Threshold mode: binary on/off
                bool isTouched = touchValue < config.threshold;

                if (isTouched != state.touched)
                {
                    if (now - state.lastChangeTime < DEBOUNCE_MS)
                        return false;

                    state.lastChangeTime = now;
                    state.touched = isTouched;
                    currentValue = isTouched ? config.max_midi_value : config.min_midi_value;
                    return true;
                }
            }

            return false;
        }

        /**
         * Maps `x` from `inMin`..`inMax` onto the output pin. An inverted range
         * (min > max) inverts the pin. Digital pins switch at the midpoint.
         */
        static void writeOutput(const PinConfig &c, long x, long inMin, long inMax)
        {
            const int level = constrain((int)map(x, inMin, inMax, 0, PWM_MAX_VALUE), 0, PWM_MAX_VALUE);
            if (c.mode == ENOMIK_OUTPUT)
                digitalWrite(c.pin, level > PWM_MAX_VALUE / 2 ? HIGH : LOW);
            else if (c.mode == ENOMIK_ANALOG_OUTPUT)
                analogWrite(c.pin, (uint8_t)level);
        }

        /** Maps signed pitch bend through the pin's min/max onto the output pin. */
        static void writePitchBend(const PinConfig &c, int bend)
        {
            // min/max are 7-bit; scale them to 14 bits like pitch-bend inputs do.
            // Center maps just below the midpoint, so digital pins rest off either way.
            writeOutput(c, bend + 8192, pitchBendFrom7Bit(c.min_midi_value),
                        pitchBendFrom7Bit(c.max_midi_value));
        }

        /** Maps a 7-bit MIDI value through the pin's min/max onto the output pin. */
        static void writeOutput(const PinConfig &c, long value)
        {
            writeOutput(c, value, c.min_midi_value, c.max_midi_value);
        }

        void initializePinHardware(const PinConfig &c)
        {
            if (c.mode == ENOMIK_OUTPUT || c.mode == ENOMIK_ANALOG_OUTPUT)
            {
                pinMode(c.pin, OUTPUT);
                // rest level: pitch bend rests at center, everything else at 0
                if (c.midi_type == MidiStatus::MIDI_PITCH_BEND)
                    writePitchBend(c, 0);
                else
                    writeOutput(c, 0);
            }
            else if (c.mode == ENOMIK_INPUT)
            {
                pinMode(c.pin, INPUT);
            }
            else if (c.mode == ENOMIK_INPUT_PULLUP)
            {
                pinMode(c.pin, INPUT_PULLUP);
            }
            else if (c.mode == ENOMIK_INPUT_TOUCH)
            {
                touchAttachInterrupt(c.pin, nullptr, 40);
            }
        }

        void sendMidiMessage(const PinConfig &config, int value)
        {
            if (!_onMIDISendRequest)
                return;

            midi_message msg;
            msg.channel = config.midi_channel;
            msg.status = config.midi_type;

            switch (config.midi_type)
            {
            case MidiStatus::MIDI_NOTE_ON:
            case MidiStatus::MIDI_NOTE_OFF:
                msg.firstByte = config.midi_note;
                if (config.mode == ENOMIK_INPUT || config.mode == ENOMIK_INPUT_PULLUP)
                {
                    msg.secondByte = value > 0 ? config.min_midi_value : config.max_midi_value;
                }
                else
                {
                    msg.secondByte = value & 0x7F;
                }
                break;

            case MidiStatus::MIDI_CONTROL_CHANGE:
                msg.firstByte = config.midi_cc;
                if (config.mode == ENOMIK_INPUT || config.mode == ENOMIK_INPUT_PULLUP)
                {
                    msg.secondByte = value > 0 ? config.min_midi_value : config.max_midi_value;
                }
                else
                {
                    msg.secondByte = value & 0x7F;
                }
                break;

            case MidiStatus::MIDI_PITCH_BEND:
            {
                // Analog input is already 14-bit; digital and touch are 7-bit.
                int pb = value;
                if (config.mode == ENOMIK_INPUT || config.mode == ENOMIK_INPUT_PULLUP)
                    pb = pitchBendFrom7Bit(value > 0 ? config.min_midi_value : config.max_midi_value);
                else if (config.mode != ENOMIK_ANALOG_INPUT)
                    pb = pitchBendFrom7Bit(value);
                pb = constrain(pb, MIDI_MIN_BEND, MIDI_MAX_BEND);
                msg.firstByte = pb & 0x7F;
                msg.secondByte = (pb >> 7) & 0x7F;
                break;
            }

            default:
                return;
            }

            _onMIDISendRequest(msg);
        }

        void upsertPinConfig(const PinConfig &config)
        {
            EspNowMidiLog::d("=== UPSERT START ===");
            EspNowMidiLog::d("Upserting pin: %u", config.pin);

            // Remove existing config for this pin
            for (size_t i = 0; i < _pinConfigs.size(); i++)
            {
                if (_pinConfigs[i].pin == config.pin)
                {
                    EspNowMidiLog::d("Removing existing config at index: %u", (unsigned)i);
                    _pinConfigs.erase(_pinConfigs.begin() + i);
                    _pinStates.erase(_pinStates.begin() + i);
                    break;
                }
            }

            // Add new config
            EspNowMidiLog::d("Adding new config for pin: %u", config.pin);
            _pinConfigs.push_back(config);
            _pinStates.push_back(PinState());
            initializePinHardware(config);

            EspNowMidiLog::d("Config count after: %u", (unsigned)_pinConfigs.size());
            savePinConfigsToPrefs(_pinConfigs);
            EspNowMidiLog::d("=== UPSERT END ===");
        }

        void savePinConfigsToPrefs(const std::vector<PinConfig> &configs)
        {
            _preferences.begin("pinconfigs", false);

            for (size_t i = 0; i < configs.size(); i++)
            {
                char key[16];
                snprintf(key, sizeof(key), "cfg%zu", i);
                uint8_t buf[8] = {
                    configs[i].pin,
                    configs[i].mode,
                    configs[i].midi_channel,
                    static_cast<uint8_t>(configs[i].midi_type),
                    configs[i].midi_cc,
                    configs[i].midi_note,
                    configs[i].min_midi_value,
                    configs[i].max_midi_value};
                _preferences.putBytes(key, buf, sizeof(buf));
            }

            _preferences.putUInt("count", configs.size());
            _preferences.end();
        }

        void saveMidiLoopbackToPrefs(bool enabled)
        {
            _preferences.begin("enomik", false);
            _preferences.putUChar("midi_lb", enabled ? 1 : 0);
            _preferences.end();
        }

        bool loadMidiLoopbackFromPrefs()
        {
            _preferences.begin("enomik", true);
            const uint8_t value = _preferences.getUChar("midi_lb", 0);
            _preferences.end();
            return value != 0;
        }

        void savePowerSaveToPrefs(bool enabled)
        {
            savePowerSavePreference(enabled);
        }

        bool loadPowerSaveFromPrefs()
        {
            return loadPowerSavePreference();
        }

        // Copies a name (at most MAX_NAME_LENGTH printable ASCII characters, others become '?')
        static void copyName(char out[MAX_NAME_LENGTH + 1], const char *name)
        {
            size_t i = 0;
            for (; name && name[i] && i < MAX_NAME_LENGTH; i++)
            {
                const uint8_t c = static_cast<uint8_t>(name[i]);
                out[i] = isNameChar(c) ? static_cast<char>(c) : '?';
            }
            out[i] = '\0';
        }

        // First bytes of the running firmware's ELF SHA-256; changes with every new build
        static void currentFirmwareId(uint8_t id[FIRMWARE_ID_LENGTH])
        {
#if defined(ENOMIK_HAS_APP_DESC)
            memcpy(id, esp_app_get_description()->app_elf_sha256, FIRMWARE_ID_LENGTH);
#elif defined(ENOMIK_HAS_OTA_APP_DESC)
            memcpy(id, esp_ota_get_app_description()->app_elf_sha256, FIRMWARE_ID_LENGTH);
#else
            // Fallback: FNV-1a of the build time
            const char *build = __DATE__ " " __TIME__;
            uint64_t hash = 1469598103934665603ULL;
            for (const char *c = build; *c; c++)
            {
                hash = (hash ^ static_cast<uint8_t>(*c)) * 1099511628211ULL;
            }
            for (size_t i = 0; i < FIRMWARE_ID_LENGTH; i++)
            {
                id[i] = static_cast<uint8_t>(hash >> (8 * i));
            }
#endif
        }

        void saveFirmwareIdToPrefs()
        {
            uint8_t id[FIRMWARE_ID_LENGTH];
            currentFirmwareId(id);
            _preferences.begin("enomik", false);
            _preferences.putBytes("name_fw", id, FIRMWARE_ID_LENGTH);
            _preferences.end();
        }

        // Stored as a fixed MAX_NAME_LENGTH + 1 byte buffer; empty string = no override
        void saveNameToPrefs()
        {
            char buf[MAX_NAME_LENGTH + 1] = {};
            copyName(buf, _nameOverride);
            _preferences.begin("enomik", false);
            _preferences.putBytes("name", buf, sizeof(buf));
            _preferences.end();
        }

        // Loads the app name override. A new firmware (different ELF hash) drops it,
        // so the sketch's setName() default applies again after flashing.
        void loadNameFromPrefs()
        {
            _nameOverride[0] = '\0';

            uint8_t current[FIRMWARE_ID_LENGTH];
            uint8_t stored[FIRMWARE_ID_LENGTH];
            currentFirmwareId(current);

            _preferences.begin("enomik", false);
            const bool sameFirmware =
                _preferences.getBytes("name_fw", stored, FIRMWARE_ID_LENGTH) == FIRMWARE_ID_LENGTH &&
                memcmp(stored, current, FIRMWARE_ID_LENGTH) == 0;

            if (sameFirmware)
            {
                char buf[MAX_NAME_LENGTH + 1] = {};
                _preferences.getBytes("name", buf, sizeof(buf));
                copyName(_nameOverride, buf);
            }
            else
            {
                char empty[MAX_NAME_LENGTH + 1] = {};
                _preferences.putBytes("name", empty, sizeof(empty));
                _preferences.putBytes("name_fw", current, FIRMWARE_ID_LENGTH);
                EspNowMidiLog::i("New firmware: client name reset to '%s'", _defaultName);
            }
            _preferences.end();
        }

        std::vector<PinConfig> loadPinConfigsFromPrefs()
        {
            std::vector<PinConfig> out;

            _preferences.begin("pinconfigs", true);
            size_t count = _preferences.getUInt("count", 0);

            for (size_t i = 0; i < count; i++)
            {
                char key[16];
                snprintf(key, sizeof(key), "cfg%zu", i);
                uint8_t buf[8];

                if (_preferences.getBytes(key, buf, 8) == 8)
                {
                    PinConfig cfg(buf[0], buf[1]);
                    cfg.midi_channel = buf[2];
                    cfg.midi_type = static_cast<MidiStatus>(buf[3]);
                    cfg.midi_cc = buf[4];
                    cfg.midi_note = buf[5];
                    cfg.min_midi_value = buf[6];
                    cfg.max_midi_value = buf[7];
                    out.push_back(cfg);
                }
            }

            _preferences.end();
            return out;
        }

        // Note: This is only used internally by SysExHandler now
        std::function<void(midi_sysex_message)> _onSysExSendRequest;
    };
}