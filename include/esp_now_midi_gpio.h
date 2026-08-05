#pragma once
/** Portable pin I/O for Arduino and ESP-IDF: digital, PWM, ADC, capacitive touch. */

#ifdef ARDUINO
#include <Arduino.h>
#elif defined(ESP_PLATFORM)
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/touch_pad.h"
#include "esp_adc/adc_oneshot.h"
#include "./esp_now_midi_log.h"
#include <cstdint>

#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define INPUT 0
#define INPUT_PULLUP 2

inline void pinMode(uint8_t pin, int mode)
{
    gpio_num_t gpio = static_cast<gpio_num_t>(pin);
    gpio_reset_pin(gpio);

    if (mode == OUTPUT)
    {
        gpio_set_direction(gpio, GPIO_MODE_OUTPUT);
    }
    else
    {
        gpio_set_direction(gpio, GPIO_MODE_INPUT);
        if (mode == INPUT_PULLUP)
        {
            gpio_pullup_en(gpio);
        }
        else
        {
            gpio_pullup_dis(gpio);
        }
    }
}

inline void digitalWrite(uint8_t pin, int value)
{
    gpio_set_level(static_cast<gpio_num_t>(pin), value);
}

inline int digitalRead(uint8_t pin)
{
    return gpio_get_level(static_cast<gpio_num_t>(pin));
}

// --- Analog output (PWM via LEDC) -------------------------------------------

namespace esp_now_midi_gpio_detail
{

constexpr int LEDC_PWM_CHANNEL_COUNT = 8;
constexpr int LEDC_PWM_FREQUENCY_HZ = 1000;

struct LedcChannelSlot
{
    uint8_t pin = 0xFF;
    bool used = false;
};

inline LedcChannelSlot &ledcSlots()
{
    static LedcChannelSlot slots[LEDC_PWM_CHANNEL_COUNT];
    return *slots;
}

inline bool &ledcTimerReady()
{
    static bool ready = false;
    return ready;
}

// Returns the LEDC channel bound to `pin`, configuring a new one on first use.
// Returns -1 if every channel is already taken.
inline int acquireLedcChannel(uint8_t pin)
{
    LedcChannelSlot *slots = &ledcSlots();

    for (int i = 0; i < LEDC_PWM_CHANNEL_COUNT; i++)
    {
        if (slots[i].used && slots[i].pin == pin)
        {
            return i;
        }
    }

    int freeIndex = -1;
    for (int i = 0; i < LEDC_PWM_CHANNEL_COUNT; i++)
    {
        if (!slots[i].used)
        {
            freeIndex = i;
            break;
        }
    }

    if (freeIndex < 0)
    {
        EspNowMidiLog::e("esp_now_midi_gpio: no free LEDC channel for pin %u", pin);
        return -1;
    }

    if (!ledcTimerReady())
    {
        ledc_timer_config_t timerCfg = {};
        timerCfg.speed_mode = LEDC_LOW_SPEED_MODE;
        timerCfg.timer_num = LEDC_TIMER_0;
        timerCfg.duty_resolution = LEDC_TIMER_8_BIT;
        timerCfg.freq_hz = LEDC_PWM_FREQUENCY_HZ;
        timerCfg.clk_cfg = LEDC_AUTO_CLK;
        ledc_timer_config(&timerCfg);
        ledcTimerReady() = true;
    }

    ledc_channel_config_t channelCfg = {};
    channelCfg.gpio_num = pin;
    channelCfg.speed_mode = LEDC_LOW_SPEED_MODE;
    channelCfg.channel = static_cast<ledc_channel_t>(freeIndex);
    channelCfg.timer_sel = LEDC_TIMER_0;
    channelCfg.duty = 0;
    channelCfg.hpoint = 0;
    ledc_channel_config(&channelCfg);

    slots[freeIndex].pin = pin;
    slots[freeIndex].used = true;
    return freeIndex;
}

} // namespace esp_now_midi_gpio_detail

inline void analogWrite(uint8_t pin, uint8_t value)
{
    int channel = esp_now_midi_gpio_detail::acquireLedcChannel(pin);
    if (channel < 0)
    {
        return;
    }
    ledc_set_duty(LEDC_LOW_SPEED_MODE, static_cast<ledc_channel_t>(channel), value);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, static_cast<ledc_channel_t>(channel));
}

// --- Analog input (ADC oneshot) ---------------------------------------------

namespace esp_now_midi_gpio_detail
{

inline adc_oneshot_unit_handle_t *adcUnitHandles()
{
    static adc_oneshot_unit_handle_t handles[SOC_ADC_PERIPH_NUM] = {};
    return handles;
}

inline bool *adcChannelConfigured()
{
    static bool configured[SOC_ADC_PERIPH_NUM][SOC_ADC_MAX_CHANNEL_NUM] = {};
    return &configured[0][0];
}

} // namespace esp_now_midi_gpio_detail

// Requested bit width is a no-op: each channel is already configured with
// ADC_BITWIDTH_DEFAULT, which equals the per-chip resolution enomik_io.h's
// own ADC_RESOLUTION/ADC_MAX_VALUE #if block assumes.
inline void analogReadResolution(int /*bits*/) {}

inline int analogRead(uint8_t pin)
{
    adc_unit_t unit;
    adc_channel_t channel;
    if (adc_oneshot_io_to_channel(pin, &unit, &channel) != ESP_OK)
    {
        EspNowMidiLog::e("esp_now_midi_gpio: pin %u has no ADC channel", pin);
        return 0;
    }

    adc_oneshot_unit_handle_t &handle = esp_now_midi_gpio_detail::adcUnitHandles()[unit];
    if (handle == nullptr)
    {
        adc_oneshot_unit_init_cfg_t unitCfg = {};
        unitCfg.unit_id = unit;
        adc_oneshot_new_unit(&unitCfg, &handle);
    }

    bool *configured = esp_now_midi_gpio_detail::adcChannelConfigured() + (unit * SOC_ADC_MAX_CHANNEL_NUM);
    if (!configured[channel])
    {
        adc_oneshot_chan_cfg_t chanCfg = {};
        chanCfg.bitwidth = ADC_BITWIDTH_DEFAULT;
        chanCfg.atten = ADC_ATTEN_DB_12;
        adc_oneshot_config_channel(handle, channel, &chanCfg);
        configured[channel] = true;
    }

    int raw = 0;
    adc_oneshot_read(handle, channel, &raw);
    return raw;
}

// --- Capacitive touch --------------------------------------------------------
//
// IDF has no GPIO->touch-channel lookup (unlike adc_oneshot_io_to_channel for
// ADC), so this table is hand-built from each chip's touch pinout. Raw touch
// value polarity also differs by chip: classic ESP32 reads LOWER on touch,
// S2/S3 read HIGHER on touch. Arduino-ESP32's touchRead() normalizes this to
// "always lower on touch"; we replicate that so enomik_io.h's
// `touchValue < threshold` comparison keeps working unmodified.

namespace esp_now_midi_gpio_detail
{

#if CONFIG_IDF_TARGET_ESP32
constexpr bool TOUCH_RAW_INCREASES_ON_TOUCH = false;
inline bool gpioToTouchChannel(uint8_t pin, touch_pad_t &out)
{
    switch (pin)
    {
    case 4: out = TOUCH_PAD_NUM0; return true;
    case 0: out = TOUCH_PAD_NUM1; return true;
    case 2: out = TOUCH_PAD_NUM2; return true;
    case 15: out = TOUCH_PAD_NUM3; return true;
    case 13: out = TOUCH_PAD_NUM4; return true;
    case 12: out = TOUCH_PAD_NUM5; return true;
    case 14: out = TOUCH_PAD_NUM6; return true;
    case 27: out = TOUCH_PAD_NUM7; return true;
    case 33: out = TOUCH_PAD_NUM8; return true;
    case 32: out = TOUCH_PAD_NUM9; return true;
    default: return false;
    }
}
#elif CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32S3
constexpr bool TOUCH_RAW_INCREASES_ON_TOUCH = true;
inline bool gpioToTouchChannel(uint8_t pin, touch_pad_t &out)
{
    if (pin >= 1 && pin <= 14)
    {
        out = static_cast<touch_pad_t>(pin - 1);
        return true;
    }
    return false;
}
#else
constexpr bool TOUCH_RAW_INCREASES_ON_TOUCH = false;
inline bool gpioToTouchChannel(uint8_t, touch_pad_t &)
{
    return false; // No touch peripheral on this target (e.g. ESP32-C3).
}
#endif

inline bool &touchDriverInitialized()
{
    static bool initialized = false;
    return initialized;
}

} // namespace esp_now_midi_gpio_detail

inline void touchAttachInterrupt(uint8_t pin, void (*callback)(void), int /*threshold*/)
{
    (void)callback; // enomik_io.h always passes nullptr: polling-only, no ISR.

    touch_pad_t channel;
    if (!esp_now_midi_gpio_detail::gpioToTouchChannel(pin, channel))
    {
        EspNowMidiLog::e("esp_now_midi_gpio: pin %u has no touch channel on this target", pin);
        return;
    }

    if (!esp_now_midi_gpio_detail::touchDriverInitialized())
    {
        touch_pad_init();
        esp_now_midi_gpio_detail::touchDriverInitialized() = true;
    }

    touch_pad_config(channel);
}

inline int touchRead(uint8_t pin)
{
    touch_pad_t channel;
    if (!esp_now_midi_gpio_detail::gpioToTouchChannel(pin, channel))
    {
        return 0;
    }

    uint32_t raw = 0;
    touch_pad_read_raw_data(channel, &raw);

    // Normalize polarity to match Arduino-ESP32: always lower on touch.
    if (esp_now_midi_gpio_detail::TOUCH_RAW_INCREASES_ON_TOUCH)
    {
        constexpr uint32_t TOUCH_RAW_CEILING = 1000;
        raw = (raw < TOUCH_RAW_CEILING) ? (TOUCH_RAW_CEILING - raw) : 0;
    }

    return static_cast<int>(raw);
}

#endif
