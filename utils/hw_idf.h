#pragma once

// Requires C++17 (inline variables) and ESP-IDF 5.x.
// Call PeerStorage::initNVS() and hw::beginHardware() once at app startup
// before using any IO or storage functions.

#include "hw_backend.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/ledc.h"
#if defined(CONFIG_IDF_TARGET_ESP32)
  #include "driver/touch_sensor.h"
#endif

namespace enomik {
namespace hw {

// ── LEDC (PWM / analogWrite) ──────────────────────────────────────────────────

static constexpr ledc_timer_t      _LEDC_TIMER    = LEDC_TIMER_0;
static constexpr ledc_mode_t       _LEDC_MODE     = LEDC_LOW_SPEED_MODE;
static constexpr uint32_t          _LEDC_FREQ_HZ  = 5000;
static constexpr ledc_timer_bit_t  _LEDC_DUTY_RES = LEDC_TIMER_8_BIT;
static constexpr int               _LEDC_CH_MAX   = 8;

struct _LedcEntry { uint8_t pin; ledc_channel_t channel; bool used; };
inline _LedcEntry _ledcMap[_LEDC_CH_MAX] = {};

inline ledc_channel_t _allocLedcChannel(uint8_t pin) {
    for (int i = 0; i < _LEDC_CH_MAX; i++) {
        if (_ledcMap[i].used && _ledcMap[i].pin == pin)
            return _ledcMap[i].channel;
    }
    for (int i = 0; i < _LEDC_CH_MAX; i++) {
        if (!_ledcMap[i].used) {
            _ledcMap[i] = { pin, (ledc_channel_t)i, true };
            return (ledc_channel_t)i;
        }
    }
    return LEDC_CHANNEL_0;  // all channels exhausted, reuse ch 0
}

// ── ADC (analogRead) ─────────────────────────────────────────────────────────

inline adc_oneshot_unit_handle_t _adcHandle = nullptr;

// Maps GPIO number to ADC1 channel for each supported ESP32 variant.
inline adc_channel_t _gpioToAdcChannel(uint8_t gpio) {
#if defined(CONFIG_IDF_TARGET_ESP32)
    switch (gpio) {
        case 36: return ADC_CHANNEL_0;
        case 37: return ADC_CHANNEL_1;
        case 38: return ADC_CHANNEL_2;
        case 39: return ADC_CHANNEL_3;
        case 32: return ADC_CHANNEL_4;
        case 33: return ADC_CHANNEL_5;
        case 34: return ADC_CHANNEL_6;
        case 35: return ADC_CHANNEL_7;
        default: return ADC_CHANNEL_0;
    }
#elif defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S3)
    if (gpio >= 1 && gpio <= 10) return (adc_channel_t)(gpio - 1);
    return ADC_CHANNEL_0;
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
    if (gpio <= 4) return (adc_channel_t)gpio;
    return ADC_CHANNEL_0;
#else
    return ADC_CHANNEL_0;
#endif
}

// ── Public API ────────────────────────────────────────────────────────────────

inline unsigned long millis() {
    return (unsigned long)(esp_timer_get_time() / 1000ULL);
}

// Call once at startup before any IO operations.
inline void beginHardware() {
    // ADC oneshot unit (ADC1 only — ADC2 is shared with WiFi)
    adc_oneshot_unit_init_cfg_t adcCfg = {};
    adcCfg.unit_id = ADC_UNIT_1;
    adc_oneshot_new_unit(&adcCfg, &_adcHandle);

    // Single shared LEDC timer for all PWM outputs
    ledc_timer_config_t timerCfg = {};
    timerCfg.speed_mode      = _LEDC_MODE;
    timerCfg.timer_num       = _LEDC_TIMER;
    timerCfg.duty_resolution = _LEDC_DUTY_RES;
    timerCfg.freq_hz         = _LEDC_FREQ_HZ;
    timerCfg.clk_cfg         = LEDC_AUTO_CLK;
    ledc_timer_config(&timerCfg);

#if defined(CONFIG_IDF_TARGET_ESP32)
    touch_pad_init();
    touch_pad_set_voltage(TOUCH_HVOLT_2V7, TOUCH_LVOLT_0V5, TOUCH_HVOLT_ATTEN_1V);
#endif
}

inline void initPin(const PinConfig& c) {
    if (c.mode == ENOMIK_OUTPUT) {
        gpio_config_t cfg = {};
        cfg.pin_bit_mask = 1ULL << c.pin;
        cfg.mode         = GPIO_MODE_OUTPUT;
        cfg.pull_up_en   = GPIO_PULLUP_DISABLE;
        cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
        cfg.intr_type    = GPIO_INTR_DISABLE;
        gpio_config(&cfg);

    } else if (c.mode == ENOMIK_ANALOG_OUTPUT) {
        ledc_channel_t ch = _allocLedcChannel(c.pin);
        ledc_channel_config_t chCfg = {};
        chCfg.speed_mode = _LEDC_MODE;
        chCfg.channel    = ch;
        chCfg.timer_sel  = _LEDC_TIMER;
        chCfg.intr_type  = LEDC_INTR_DISABLE;
        chCfg.gpio_num   = c.pin;
        chCfg.duty       = 0;
        chCfg.hpoint     = 0;
        ledc_channel_config(&chCfg);

    } else if (c.mode == ENOMIK_INPUT) {
        gpio_config_t cfg = {};
        cfg.pin_bit_mask = 1ULL << c.pin;
        cfg.mode         = GPIO_MODE_INPUT;
        cfg.pull_up_en   = GPIO_PULLUP_DISABLE;
        cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
        cfg.intr_type    = GPIO_INTR_DISABLE;
        gpio_config(&cfg);

    } else if (c.mode == ENOMIK_INPUT_PULLUP) {
        gpio_config_t cfg = {};
        cfg.pin_bit_mask = 1ULL << c.pin;
        cfg.mode         = GPIO_MODE_INPUT;
        cfg.pull_up_en   = GPIO_PULLUP_ENABLE;
        cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
        cfg.intr_type    = GPIO_INTR_DISABLE;
        gpio_config(&cfg);

    } else if (c.mode == ENOMIK_ANALOG_INPUT) {
        // Configure ADC channel attenuation and bit-width for this pin.
        // ADC_ATTEN_DB_12 covers 0–3.3 V; use ADC_ATTEN_DB_11 on IDF < 5.0.
        adc_oneshot_chan_cfg_t chanCfg = {};
        chanCfg.atten    = ADC_ATTEN_DB_12;
        chanCfg.bitwidth = ADC_BITWIDTH_DEFAULT;
        adc_oneshot_config_channel(_adcHandle, _gpioToAdcChannel(c.pin), &chanCfg);

    } else if (c.mode == ENOMIK_INPUT_TOUCH) {
#if defined(CONFIG_IDF_TARGET_ESP32)
        touch_pad_config((touch_pad_t)c.pin, 0);
#endif
        // TODO: implement touch for ESP32-S2/S3 using touch_sensor_v2 API
    }
}

inline int readDigital(uint8_t pin) {
    return gpio_get_level((gpio_num_t)pin);
}

inline int readAnalog(uint8_t pin) {
    int val = 0;
    if (_adcHandle)
        adc_oneshot_read(_adcHandle, _gpioToAdcChannel(pin), &val);
    return val;
}

inline int readTouch(uint8_t pin) {
#if defined(CONFIG_IDF_TARGET_ESP32)
    uint16_t val = 0;
    touch_pad_read_raw_data((touch_pad_t)pin, &val);
    return (int)val;
#else
    // TODO: implement for ESP32-S2/S3 using touch_sensor_v2 API
    return 0;
#endif
}

inline void writeDigital(uint8_t pin, int val) {
    gpio_set_level((gpio_num_t)pin, val ? 1 : 0);
}

inline void writeAnalog(uint8_t pin, int val) {
    for (int i = 0; i < _LEDC_CH_MAX; i++) {
        if (_ledcMap[i].used && _ledcMap[i].pin == pin) {
            ledc_set_duty(_LEDC_MODE, _ledcMap[i].channel, (uint32_t)val);
            ledc_update_duty(_LEDC_MODE, _ledcMap[i].channel);
            return;
        }
    }
}

} // namespace hw
} // namespace enomik
