#pragma once

#include <vector>
#include <Preferences.h>
#include "esp_now_midi.h"
#include "utils/mac.h"
#include "enomik_sysex.h"
#include "./enomik_pinconfig.h"

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
        bool touched = false;
    };

    class IO
    {
    public:
        static constexpr unsigned long DEBOUNCE_MS = 4;
        static constexpr int ANALOG_THRESHOLD = 2;
        static constexpr unsigned long ANALOG_MIN_INTERVAL = 5;
        static constexpr float SMOOTHING_FACTOR = 0.3f;

        void begin()
        {
            analogReadResolution(ADC_RESOLUTION);
            _pinConfigs = loadPinConfigsFromPrefs();
            _pinStates.clear();

            for (const auto &config : _pinConfigs)
            {
                initializePinHardware(config);
                _pinStates.push_back(PinState());
            }

            setupSysExHandlers();
        }

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

        // MIDI Input Handlers
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

        void onProgramChange(byte channel, byte program)
        {
            // Reserved for future use
        }

        void onSysEx(const uint8_t *data, uint16_t length)
        {
            _sysexHandler.handleSysEx(data, length);
        }

        // External callbacks (set by main application)
        void setOnMIDISendRequest(std::function<void(midi_message)> callback)
        {
            _onMIDISendRequest = callback;
        }

        void setOnAddPeerRequest(std::function<void(uint8_t mac[])> callback)
        {
            _onAddPeerRequest = callback;
        }

        void setOnGetPeersRequest(std::function<void()> callback)
        {
            _onGetPeersRequest = callback;
        }

        void setOnResetRequest(std::function<void()> callback)
        {
            _onResetRequest = callback;
        }
        void setOnSysExSendRequest(std::function<void(midi_sysex_message)> callback)
        {
            _onSysExSendRequest = callback;
        }

        void printPinConfigs()
        {
            Serial.println("=== Pin Configurations ===");
            for (size_t i = 0; i < _pinConfigs.size(); i++)
            {
                const auto &cfg = _pinConfigs[i];
                Serial.print("Pin: ");
                Serial.print(cfg.pin);
                Serial.print(" | Mode: ");
                Serial.print(cfg.mode);
                Serial.print(" | MIDI Channel: ");
                Serial.print(cfg.midi_channel);
                Serial.print(" | MIDI Type: ");
                Serial.print(static_cast<uint8_t>(cfg.midi_type));
                Serial.print(" | CC: ");
                Serial.print(cfg.midi_cc);
                Serial.print(" | Note: ");
                Serial.print(cfg.midi_note);
                Serial.print(" | Min MIDI: ");
                Serial.print(cfg.min_midi_value);
                Serial.print(" | Max MIDI: ");
                Serial.println(cfg.max_midi_value);
            }
            Serial.println("==========================");
        }

    private:
        std::vector<PinConfig> _pinConfigs;
        std::vector<PinState> _pinStates;
        SysExHandler _sysexHandler;
        Preferences _preferences;

        // External callbacks
        std::function<void(midi_message)> _onMIDISendRequest;
        std::function<void(uint8_t mac[])> _onAddPeerRequest;
        std::function<void()> _onGetPeersRequest;
        std::function<void()> _onResetRequest;

        void setupSysExHandlers()
        {
            // Handler for setting pin configuration
            _sysexHandler.setOnSetPinConfig([this](const PinConfig &cfg)
                                            {
                Serial.println("SysEx: Setting pin config");
                upsertPinConfig(cfg);
                _sysexHandler.sendPinConfigResponse(cfg); });

            // Handler for getting single pin configuration
            _sysexHandler.setOnGetPinConfig([this](uint8_t pin)
                                            {
                Serial.print("SysEx: Getting config for pin ");
                Serial.println(pin);
                for (const auto &cfg : _pinConfigs)
                {
                    if (cfg.pin == pin)
                    {
                        _sysexHandler.sendPinConfigResponse(cfg);
                        return;
                    }
                }
                Serial.println("SysEx: Pin config not found"); });

            // Handler for getting all pin configurations
            _sysexHandler.setOnGetAllPinConfigs([this]()
                                                {
                Serial.println("SysEx: Getting all pin configs");
                for (const auto &cfg : _pinConfigs)
                {
                    _sysexHandler.sendPinConfigResponse(cfg);
                } });

            // Handler for deleting pin configuration
            _sysexHandler.setOnDeletePinConfig([this](uint8_t pin)
                                               {
                Serial.print("SysEx: Deleting config for pin ");
                Serial.println(pin);
                
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
                } });

            // Handler for clearing all pin configurations
            _sysexHandler.setOnClearPinConfigs([this]()
                                               {
                Serial.println("SysEx: Clearing all pin configs");
                _pinConfigs.clear();
                _pinStates.clear();
                savePinConfigsToPrefs(_pinConfigs);
                _sysexHandler.sendSimpleResponse(SysExCommand::CLEAR_PIN_CONFIGS); });

            // Handler for getting MAC address
            _sysexHandler.setOnGetMAC([this]()
                                      {
                Serial.println("SysEx: Getting MAC address");
                uint8_t mac[6];
                esp_read_mac(mac, ESP_MAC_WIFI_STA);
                _sysexHandler.sendMACResponse(mac); });

            // Handler for adding peer
            _sysexHandler.setOnAddPeer([this](const uint8_t mac[6])
                                       {
                Serial.print("SysEx: Adding peer ");
                for (int i = 0; i < 6; i++)
                {
                    Serial.print(mac[i], HEX);
                    if (i < 5) Serial.print(":");
                }
                Serial.println();
                
                if (_onAddPeerRequest)
                {
                    // Need to cast away const for the callback
                    uint8_t macCopy[6];
                    memcpy(macCopy, mac, 6);
                    _onAddPeerRequest(macCopy);
                } });

            // Handler for getting peers
            _sysexHandler.setOnGetPeers([this]()
                                        {
                Serial.println("SysEx: Getting peers list");
                if (_onGetPeersRequest)
                {
                    _onGetPeersRequest();
                } });

            // Handler for system reset
            _sysexHandler.setOnReset([this]()
                                     {
                Serial.println("SysEx: Performing system reset");
                
                // Clear in-memory configurations
                _pinConfigs.clear();
                _pinStates.clear();

                // Clear stored preferences
                _preferences.begin("pinconfigs", false);
                _preferences.clear();
                _preferences.end();

                if (_onResetRequest)
                {
                    _onResetRequest();
                }
                
                Serial.println("System reset complete"); });

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

        bool processAnalogInput(const PinConfig &config, PinState &state,
                                unsigned long now, int &currentValue)
        {
            int rawValue = analogRead(config.pin);

            // Initialize smoothed value on first read
            if (state.lastValue == -1)
                state.smoothedValue = rawValue;
            else
                state.smoothedValue = (SMOOTHING_FACTOR * rawValue) +
                                      (1.0f - SMOOTHING_FACTOR) * state.smoothedValue;

            // For pitch bend, preserve full ADC resolution
            if (config.midi_type == MidiStatus::MIDI_PITCH_BEND)
            {
                // Map directly to 14-bit pitch bend range (0-16383)
                currentValue = map((int)state.smoothedValue, 0, ADC_MAX_VALUE, 0, 16383);
                currentValue = constrain(currentValue, 0, 16383);

                // Use higher threshold for pitch bend since we have more resolution
                if (state.lastValue != -1 && abs(currentValue - state.lastValue) < (ANALOG_THRESHOLD * 4))
                    return false;
            }
            else
            {
                // For other MIDI types, use standard 7-bit range
                int mappedValue = map((int)state.smoothedValue, 0, ADC_MAX_VALUE,
                                      config.min_midi_value, config.max_midi_value);
                currentValue = constrain(mappedValue, 0, 127);

                if (state.lastValue != -1 && abs(currentValue - state.lastValue) < ANALOG_THRESHOLD)
                    return false;
            }

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
                // No threshold set, send scaled touch values with smoothing
                if (state.lastValue == -1)
                    state.smoothedValue = touchValue;
                else
                    state.smoothedValue = (SMOOTHING_FACTOR * touchValue) +
                                          (1.0f - SMOOTHING_FACTOR) * state.smoothedValue;

                // Invert mapping: lower touch value (stronger touch) = higher MIDI value
                int mappedValue = map((int)state.smoothedValue, 0, 100, 127, 0);
                mappedValue = constrain(mappedValue, 0, 127);

                // Apply user's min/max range
                currentValue = map(mappedValue, 0, 127,
                                   config.min_midi_value, config.max_midi_value);
                currentValue = constrain(currentValue, config.min_midi_value, config.max_midi_value);

                if (state.lastValue != -1 && abs(currentValue - state.lastValue) < ANALOG_THRESHOLD)
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
            }

            _onMIDISendRequest(msg);
        }

        void upsertPinConfig(const PinConfig &config)
        {
            Serial.println("=== UPSERT START ===");
            Serial.print("Upserting pin: ");
            Serial.println(config.pin);

            // Remove existing config for this pin
            for (size_t i = 0; i < _pinConfigs.size(); i++)
            {
                if (_pinConfigs[i].pin == config.pin)
                {
                    Serial.print("Removing existing config at index: ");
                    Serial.println(i);
                    _pinConfigs.erase(_pinConfigs.begin() + i);
                    _pinStates.erase(_pinStates.begin() + i);
                    break;
                }
            }

            // Add new config
            Serial.print("Adding new config for pin: ");
            Serial.println(config.pin);
            _pinConfigs.push_back(config);
            _pinStates.push_back(PinState());
            initializePinHardware(config);

            Serial.print("Config count after: ");
            Serial.println(_pinConfigs.size());
            savePinConfigsToPrefs(_pinConfigs);
            Serial.println("=== UPSERT END ===");
        }

        void savePinConfigsToPrefs(const std::vector<PinConfig> &configs)
        {
            _preferences.begin("pinconfigs", false);

            for (size_t i = 0; i < configs.size(); i++)
            {
                String key = "cfg" + String(i);
                uint8_t buf[8] = {
                    configs[i].pin,
                    configs[i].mode,
                    configs[i].midi_channel,
                    static_cast<uint8_t>(configs[i].midi_type),
                    configs[i].midi_cc,
                    configs[i].midi_note,
                    configs[i].min_midi_value,
                    configs[i].max_midi_value};
                _preferences.putBytes(key.c_str(), buf, sizeof(buf));
            }

            _preferences.putUInt("count", configs.size());
            _preferences.end();
        }

        std::vector<PinConfig> loadPinConfigsFromPrefs()
        {
            std::vector<PinConfig> out;

            _preferences.begin("pinconfigs", true);
            size_t count = _preferences.getUInt("count", 0);

            for (size_t i = 0; i < count; i++)
            {
                String key = "cfg" + String(i);
                uint8_t buf[8];

                if (_preferences.getBytes(key.c_str(), buf, 8) == 8)
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