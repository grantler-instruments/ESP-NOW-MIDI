#pragma once

#include <vector>
#include <cstdlib>
#ifdef ARDUINO
  #include <Preferences.h>
  #include "utils/hw_arduino.h"
#else
  #include "utils/hw_idf.h"
  #include "nvs_flash.h"
  #include "nvs.h"
#endif
#include "utils/mac.h"
#include "utils/log.h"
#include "enomik_sysex.h"
#include "./enomik_pinconfig.h"

// ADC_RESOLUTION, ADC_MAX_VALUE, PWM_MAX_VALUE are defined in utils/hw_backend.h
// (included transitively above)

namespace enomik
{
    // Pin mode constants are defined in enomik_pinconfig.h (enomik namespace)

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

#ifndef ARDUINO
        // Initialize the NVS flash partition. Call once at app startup before
        // begin(). Handles the erase-and-reinit case automatically.
        static bool initNVS() {
            esp_err_t ret = nvs_flash_init();
            if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
                enomik_log_debug("IO: NVS partition truncated or version changed, erasing");
                if (nvs_flash_erase() != ESP_OK) {
                    enomik_log_error("IO: Failed to erase NVS flash");
                    return false;
                }
                ret = nvs_flash_init();
            }
            if (ret != ESP_OK) {
                enomik_log_error("IO: Failed to initialize NVS flash");
                return false;
            }
            return true;
        }
#endif

        void begin()
        {
            hw::beginHardware();
#ifndef ARDUINO
            nvs_open("pinconfigs", NVS_READWRITE, &_nvsHandle);
#endif
            _pinConfigs = loadPinConfigsFromPrefs();
            _pinStates.clear();

            for (const auto &config : _pinConfigs)
            {
                hw::initPin(config);
                _pinStates.push_back(PinState());
            }

            setupSysExHandlers();
        }

        void loop()
        {
            unsigned long now = hw::millis();

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
        void onNoteOn(uint8_t channel, uint8_t note, uint8_t velocity)
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
                        hw::writeDigital(config.pin, 1);
                    }
                    else if (config.mode == ENOMIK_ANALOG_OUTPUT)
                    {
                        int mappedValue = (int)hw::map(velocity, config.min_midi_value,
                                                       config.max_midi_value, 0, PWM_MAX_VALUE);
                        mappedValue = (int)hw::clamp(mappedValue, 0, PWM_MAX_VALUE);
                        hw::writeAnalog(config.pin, (uint8_t)mappedValue);
                    }
                }
            }
        }

        void onNoteOff(uint8_t channel, uint8_t note, uint8_t velocity)
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
                        hw::writeDigital(config.pin, 0);
                    }
                    else if (config.mode == ENOMIK_ANALOG_OUTPUT)
                    {
                        hw::writeAnalog(config.pin, 0);
                    }
                }
            }
        }

        void onPitchBend(uint8_t channel, int bend)
        {
            for (auto &config : _pinConfigs)
            {
                if (config.midi_type == MidiStatus::MIDI_PITCH_BEND &&
                    config.midi_channel == channel)
                {
                    if (config.mode == ENOMIK_OUTPUT)
                    {
                        hw::writeDigital(config.pin, bend >= 8192 ? 1 : 0);
                    }
                    else if (config.mode == ENOMIK_ANALOG_OUTPUT)
                    {
                        int mappedValue = (int)hw::map(bend, 0, 16383, 0, PWM_MAX_VALUE);
                        hw::writeAnalog(config.pin, (int)hw::clamp(mappedValue, 0, PWM_MAX_VALUE));
                    }
                }
            }
        }

        void onControlChange(uint8_t channel, uint8_t control, uint8_t value)
        {
            for (auto &config : _pinConfigs)
            {
                if (config.midi_type == MidiStatus::MIDI_CONTROL_CHANGE &&
                    config.midi_channel == channel &&
                    config.midi_cc == control)
                {
                    if (config.mode == ENOMIK_OUTPUT)
                    {
                        hw::writeDigital(config.pin, value > 63 ? 1 : 0);
                    }
                    else if (config.mode == ENOMIK_ANALOG_OUTPUT)
                    {
                        int mappedValue = (int)hw::map(value, config.min_midi_value,
                                                       config.max_midi_value, 0, PWM_MAX_VALUE);
                        hw::writeAnalog(config.pin, (int)hw::clamp(mappedValue, 0, PWM_MAX_VALUE));
                    }
                }
            }
        }

        void onProgramChange(uint8_t channel, uint8_t program)
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

        void setOnAddPeerRequest(std::function<bool(uint8_t mac[])> callback)
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
            enomik_log("=== Pin Configurations ===");
            for (size_t i = 0; i < _pinConfigs.size(); i++)
            {
                const auto &cfg = _pinConfigs[i];
                enomik_log("Pin: %d | Mode: %d | Ch: %d | Type: 0x%02X | CC: %d | Note: %d | Min: %d | Max: %d",
                    cfg.pin, cfg.mode, cfg.midi_channel,
                    static_cast<uint8_t>(cfg.midi_type),
                    cfg.midi_cc, cfg.midi_note,
                    cfg.min_midi_value, cfg.max_midi_value);
            }
            enomik_log("==========================");
        }

    private:
        std::vector<PinConfig> _pinConfigs;
        std::vector<PinState> _pinStates;
        SysExHandler _sysexHandler;

#ifdef ARDUINO
        Preferences _preferences;
#else
        nvs_handle_t _nvsHandle = 0;
#endif

        // External callbacks
        std::function<void(midi_message)> _onMIDISendRequest;
        std::function<bool(uint8_t mac[])> _onAddPeerRequest;
        std::function<void()> _onGetPeersRequest;
        std::function<void()> _onResetRequest;

        void setupSysExHandlers()
        {
            // Handler for setting pin configuration
            _sysexHandler.setOnSetPinConfig([this](const PinConfig &cfg)
                                            {
                enomik_log_debug("SysEx: Setting pin config");
                upsertPinConfig(cfg);
                _sysexHandler.sendPinConfigResponse(cfg); });

            // Handler for getting single pin configuration
            _sysexHandler.setOnGetPinConfig([this](uint8_t pin)
                                            {
                enomik_log_debug("SysEx: Getting config for pin %d", pin);
                for (const auto &cfg : _pinConfigs)
                {
                    if (cfg.pin == pin)
                    {
                        _sysexHandler.sendPinConfigResponse(cfg);
                        return;
                    }
                }
                enomik_log_debug("SysEx: Pin config not found"); });

            // Handler for getting all pin configurations
            _sysexHandler.setOnGetAllPinConfigs([this]()
                                                {
                enomik_log_debug("SysEx: Getting all pin configs");
                for (const auto &cfg : _pinConfigs)
                {
                    _sysexHandler.sendPinConfigResponse(cfg);
                } });

            // Handler for deleting pin configuration
            _sysexHandler.setOnDeletePinConfig([this](uint8_t pin)
                                               {
                enomik_log_debug("SysEx: Deleting config for pin %d", pin);
                
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
                enomik_log_debug("SysEx: Clearing all pin configs");
                _pinConfigs.clear();
                _pinStates.clear();
                savePinConfigsToPrefs(_pinConfigs);
                _sysexHandler.sendSimpleResponse(SysExCommand::CLEAR_PIN_CONFIGS); });

            // Handler for getting MAC address
            _sysexHandler.setOnGetMAC([this]()
                                      {
                enomik_log_debug("SysEx: Getting MAC address");
                uint8_t mac[6];
                esp_read_mac(mac, ESP_MAC_WIFI_STA);
                _sysexHandler.sendMACResponse(mac); });

            // Handler for adding peer
            _sysexHandler.setOnAddPeer([this](const uint8_t mac[6])
                                       {
                enomik_log_debug("SysEx: Adding peer %02X:%02X:%02X:%02X:%02X:%02X",
                    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
                
                if (_onAddPeerRequest)
                {
                    uint8_t macCopy[6];
                    memcpy(macCopy, mac, 6);
                    bool success = _onAddPeerRequest(macCopy);
                    _sysexHandler.sendAddPeerResponse(success);
                }
                else
                {
                    _sysexHandler.sendAddPeerResponse(false);
                } });

            // Handler for getting peers
            _sysexHandler.setOnGetPeers([this]()
                                        {
                enomik_log_debug("SysEx: Getting peers list");
                if (_onGetPeersRequest)
                {
                    _onGetPeersRequest();
                } });

            // Handler for system reset
            _sysexHandler.setOnReset([this]()
                                     {
                enomik_log_debug("SysEx: Performing system reset");

                _pinConfigs.clear();
                _pinStates.clear();

#ifdef ARDUINO
                _preferences.begin("pinconfigs", false);
                _preferences.clear();
                _preferences.end();
#else
                nvs_erase_all(_nvsHandle);
                nvs_commit(_nvsHandle);
#endif

                if (_onResetRequest)
                {
                    _onResetRequest();
                }

                _sysexHandler.sendSimpleResponse(SysExCommand::RESET_RESPONSE);
                enomik_log_debug("System reset complete"); });

            // Handler for sending SysEx messages back out
            _sysexHandler.setOnSend([this](const midi_sysex_message &msg)
                                    {
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
            currentValue = hw::readDigital(config.pin);

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
            int rawValue = hw::readAnalog(config.pin);

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
                currentValue = (int)hw::map((int)state.smoothedValue, 0, ADC_MAX_VALUE, 0, 16383);
                currentValue = (int)hw::clamp(currentValue, 0, 16383);

                // Use higher threshold for pitch bend since we have more resolution
                if (state.lastValue != -1 && std::abs(currentValue - state.lastValue) < (ANALOG_THRESHOLD * 4))
                    return false;
            }
            else
            {
                // For other MIDI types, use standard 7-bit range
                int mappedValue = (int)hw::map((int)state.smoothedValue, 0, ADC_MAX_VALUE,
                                               config.min_midi_value, config.max_midi_value);
                currentValue = (int)hw::clamp(mappedValue, 0, 127);

                if (state.lastValue != -1 && std::abs(currentValue - state.lastValue) < ANALOG_THRESHOLD)
                    return false;
            }

            if (now - state.lastSendTime < ANALOG_MIN_INTERVAL)
                return false;

            return true;
        }

        bool processTouchInput(const PinConfig &config, PinState &state,
                               unsigned long now, int &currentValue)
        {
            int touchValue = hw::readTouch(config.pin);

            if (config.threshold == 0)
            {
                // No threshold set, send scaled touch values with smoothing
                if (state.lastValue == -1)
                    state.smoothedValue = touchValue;
                else
                    state.smoothedValue = (SMOOTHING_FACTOR * touchValue) +
                                          (1.0f - SMOOTHING_FACTOR) * state.smoothedValue;

                // Invert mapping: lower touch value (stronger touch) = higher MIDI value
                int mappedValue = (int)hw::map((int)state.smoothedValue, 0, 100, 127, 0);
                mappedValue = (int)hw::clamp(mappedValue, 0, 127);

                // Apply user's min/max range
                currentValue = (int)hw::map(mappedValue, 0, 127,
                                            config.min_midi_value, config.max_midi_value);
                currentValue = (int)hw::clamp(currentValue,
                                                  config.min_midi_value, config.max_midi_value);

                if (state.lastValue != -1 && std::abs(currentValue - state.lastValue) < ANALOG_THRESHOLD)
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
                int pb = (int)hw::map(value, 0, 127, 0, 16383);
                msg.firstByte = pb & 0x7F;
                msg.secondByte = (pb >> 7) & 0x7F;
                break;
            }
            default:
                break;
            }

            _onMIDISendRequest(msg);
        }

        void upsertPinConfig(const PinConfig &config)
        {
            enomik_log_debug("Upserting pin: %d", config.pin);

            // Remove existing config for this pin
            for (size_t i = 0; i < _pinConfigs.size(); i++)
            {
                if (_pinConfigs[i].pin == config.pin)
                {
                    enomik_log_debug("Removing existing config at index: %d", (int)i);
                    _pinConfigs.erase(_pinConfigs.begin() + i);
                    _pinStates.erase(_pinStates.begin() + i);
                    break;
                }
            }

            _pinConfigs.push_back(config);
            _pinStates.push_back(PinState());
            hw::initPin(config);

            enomik_log_debug("Config count after upsert: %d", (int)_pinConfigs.size());
            savePinConfigsToPrefs(_pinConfigs);
        }

        void savePinConfigsToPrefs(const std::vector<PinConfig> &configs)
        {
#ifdef ARDUINO
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
#else
            char key[8];
            for (size_t i = 0; i < configs.size(); i++) {
                snprintf(key, sizeof(key), "cfg%d", (int)i);
                uint8_t buf[8] = {
                    configs[i].pin,
                    configs[i].mode,
                    configs[i].midi_channel,
                    static_cast<uint8_t>(configs[i].midi_type),
                    configs[i].midi_cc,
                    configs[i].midi_note,
                    configs[i].min_midi_value,
                    configs[i].max_midi_value
                };
                nvs_set_blob(_nvsHandle, key, buf, sizeof(buf));
            }
            nvs_set_u32(_nvsHandle, "count", (uint32_t)configs.size());
            nvs_commit(_nvsHandle);
#endif
        }

        std::vector<PinConfig> loadPinConfigsFromPrefs()
        {
            std::vector<PinConfig> out;
#ifdef ARDUINO
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
#else
            uint32_t count = 0;
            nvs_get_u32(_nvsHandle, "count", &count);

            char key[8];
            for (uint32_t i = 0; i < count; i++) {
                snprintf(key, sizeof(key), "cfg%d", (int)i);
                uint8_t buf[8];
                size_t len = sizeof(buf);
                if (nvs_get_blob(_nvsHandle, key, buf, &len) == ESP_OK && len == 8) {
                    PinConfig cfg(buf[0], buf[1]);
                    cfg.midi_channel   = buf[2];
                    cfg.midi_type      = static_cast<MidiStatus>(buf[3]);
                    cfg.midi_cc        = buf[4];
                    cfg.midi_note      = buf[5];
                    cfg.min_midi_value = buf[6];
                    cfg.max_midi_value = buf[7];
                    out.push_back(cfg);
                }
            }
#endif
            return out;
        }

        // Note: This is only used internally by SysExHandler now
        std::function<void(midi_sysex_message)> _onSysExSendRequest;
    };
}
