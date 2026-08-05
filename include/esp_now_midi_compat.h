#pragma once
/** Portable fallbacks for Arduino core functions used outside Arduino. */

#ifdef ARDUINO
#include <Arduino.h>

using PortableString = String;
#elif defined(ESP_PLATFORM)
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string>

inline unsigned long millis()
{
    return static_cast<unsigned long>(esp_timer_get_time() / 1000);
}

inline void delay(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

inline long map(long x, long in_min, long in_max, long out_min, long out_max)
{
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))

using PortableString = std::string;
#else
#include <string>

using PortableString = std::string;
#endif
