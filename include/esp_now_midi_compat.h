#pragma once
/** Portable fallbacks for Arduino core functions used outside Arduino. */

#ifdef ARDUINO
#include <Arduino.h>
#elif defined(ESP_PLATFORM)
#include "esp_timer.h"

inline unsigned long millis()
{
    return static_cast<unsigned long>(esp_timer_get_time() / 1000);
}

inline long map(long x, long in_min, long in_max, long out_min, long out_max)
{
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
#endif
