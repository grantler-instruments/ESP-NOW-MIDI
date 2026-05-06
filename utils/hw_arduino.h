#pragma once

#include "hw_backend.h"
#include <Arduino.h>

namespace enomik {
namespace hw {

inline unsigned long millis() {
    return ::millis();
}

inline void beginHardware() {
    analogReadResolution(ADC_RESOLUTION);
}

inline void initPin(const PinConfig& c) {
    if (c.mode == ENOMIK_OUTPUT || c.mode == ENOMIK_ANALOG_OUTPUT) {
        ::pinMode(c.pin, OUTPUT);
        if (c.mode == ENOMIK_ANALOG_OUTPUT)
            ::analogWrite(c.pin, 0);
    } else if (c.mode == ENOMIK_INPUT) {
        ::pinMode(c.pin, INPUT);
    } else if (c.mode == ENOMIK_INPUT_PULLUP) {
        ::pinMode(c.pin, INPUT_PULLUP);
    } else if (c.mode == ENOMIK_INPUT_TOUCH) {
        touchAttachInterrupt(c.pin, nullptr, 40);
    }
    // ENOMIK_ANALOG_INPUT: no explicit pinMode needed on Arduino/ESP32
}

inline int readDigital(uint8_t pin) {
    return ::digitalRead(pin);
}

inline int readAnalog(uint8_t pin) {
    return ::analogRead(pin);
}

inline int readTouch(uint8_t pin) {
    return ::touchRead(pin);
}

inline void writeDigital(uint8_t pin, int val) {
    ::digitalWrite(pin, val);
}

inline void writeAnalog(uint8_t pin, int val) {
    ::analogWrite(pin, (uint8_t)val);
}

} // namespace hw
} // namespace enomik
