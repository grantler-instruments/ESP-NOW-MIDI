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
                    if (config.mode == ENOMIK_OUTPUT)
                    {
                        digitalWrite(config.pin, HIGH);
                    }
                    else if (config.mode == ENOMIK_ANALOG_OUTPUT)
                    {
                        int mappedValue = map(velocity, config.min_midi_value, config.max_midi_value,
                                              0, PWM_MAX_VALUE);
                        mappedValue = constrain(mappedValue, 0, PWM_MAX_VALUE);
                        analogWrite(config.pin, (uint8_t)mappedValue);
                    }
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
                    if (config.mode == ENOMIK_OUTPUT)
                    {
                        digitalWrite(config.pin, LOW);
                    }
                    else if (config.mode == ENOMIK_ANALOG_OUTPUT)
                    {
                        analogWrite(config.pin, 0);
                    }
                }
            }
        }

        /** @brief Applies raw 14-bit pitch bend (`0`–`16383`, center `8192`) to matching output pins. */
        void onPitchBend(byte channel, int bend)
        {
            for (auto &config : _pinConfigs)
            {
                if (config.midi_type == MidiStatus::MIDI_PITCH_BEND &&
                    config.midi_channel == channel)
                {
                    if (config.mode == ENOMIK_OUTPUT)
                    {
                        digitalWrite(config.pin, bend >= 8192 ? HIGH : LOW);
                    }
                    else if (config.mode == ENOMIK_ANALOG_OUTPUT)
                    {
                        int mappedValue = map(bend, 0, 16383, 0, PWM_MAX_VALUE);
                        analogWrite(config.pin, constrain(mappedValue, 0, PWM_MAX_VALUE));
                    }
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
                    if (config.mode == ENOMIK_OUTPUT)
                    {
                        digitalWrite(config.pin, value > 63 ? HIGH : LOW);
                    }
                    else if (config.mode == ENOMIK_ANALOG_OUTPUT)
                    {
                        int mappedValue = map(value, config.min_midi_value, config.max_midi_value, 0, PWM_MAX_VALUE);
                        analogWrite(config.pin, constrain(mappedValue, 0, PWM_MAX_VALUE));
                    }
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

        // External callbacks
        std::function<void(midi_message)> _onMIDISendRequest;
        std::function<AddPeerResult(uint8_t mac[])> _onAddPeerRequest;
        std::function<const uint8_t *(uint8_t index)> _onGetPeerRequest;
        std::function<void()> _onResetRequest;
        std::function<void(bool)> _onPowerSaveChanged;

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

            // Handler for system reset
            _sysexHandler.setOnReset([this]()
                                     {
                EspNowMidiLog::i("SysEx: Performing system reset");
                
                // Clear in-memory configurations
                _pinConfigs.clear();
                _pinStates.clear();
                _midiLoopback = false;
                _powerSave = false;

                // Clear stored preferences
                _preferences.begin("pinconfigs", false);
                _preferences.clear();
                _preferences.end();
                _preferences.begin("enomik", false);
                _preferences.clear();
                _preferences.end();

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
            currentValue = digitalRead(config.pin);

            if (currentValue != state.lastValue)
            {
                state.lastChangeTime = now;
                return true;
            }

            return false;
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
            int rawValue = analogRead(config.pin);
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

        void initializePinHardware(const PinConfig &c)
        {
            if (c.mode == ENOMIK_OUTPUT || c.mode == ENOMIK_ANALOG_OUTPUT)
            {
                pinMode(c.pin, OUTPUT);
                if (c.mode == ENOMIK_ANALOG_OUTPUT)
                    analogWrite(c.pin, 0);
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
                int pb = map(value, 0, 127, 0, 16383);
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
            _preferences.begin("enomik", false);
            _preferences.putUChar("pwr_save", enabled ? 1 : 0);
            _preferences.end();
        }

        bool loadPowerSaveFromPrefs()
        {
            _preferences.begin("enomik", true);
            const uint8_t value = _preferences.getUChar("pwr_save", 0);
            _preferences.end();
            return value != 0;
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