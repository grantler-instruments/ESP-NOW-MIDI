#pragma once

#include <cstdint>
#include "../enomik_pinconfig.h"

// ── ADC / PWM / Touch resolution ─────────────────────────────────────────────
#if defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S3)
  #define ADC_RESOLUTION  13
  #define ADC_MAX_VALUE   8191
  #define TOUCH_MAX_VALUE 100
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
  #define ADC_RESOLUTION  12
  #define ADC_MAX_VALUE   4095
  #define TOUCH_MAX_VALUE 100
#else  // Original ESP32
  #define ADC_RESOLUTION  12
  #define ADC_MAX_VALUE   4095
  #define TOUCH_MAX_VALUE 100
#endif

// analogWrite uses 8-bit resolution by default
#ifndef PWM_MAX_VALUE
  #define PWM_MAX_VALUE 255
#endif

namespace enomik {
namespace hw {

// Pure-math utilities — platform-independent, no Arduino/IDF dependency.
// Mirrors Arduino's map() and constrain() semantics exactly.

inline long map(long x, long il, long ih, long ol, long oh) {
    return (x - il) * (oh - ol) / (ih - il) + ol;
}

inline long constrain(long x, long lo, long hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

} // namespace hw
} // namespace enomik
