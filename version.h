#pragma once
#define ESP_NOW_MIDI_VERSION_MAJOR 0
#define ESP_NOW_MIDI_VERSION_MINOR 13
#define ESP_NOW_MIDI_VERSION_PATCH 0

#ifdef ARDUINO
#include <Arduino.h>
inline String getVersion() {
    return String(ESP_NOW_MIDI_VERSION_MAJOR) + "." +
           String(ESP_NOW_MIDI_VERSION_MINOR) + "." +
           String(ESP_NOW_MIDI_VERSION_PATCH);
}
#else
#include <string>
inline std::string getVersion() {
    return std::to_string(ESP_NOW_MIDI_VERSION_MAJOR) + "." +
           std::to_string(ESP_NOW_MIDI_VERSION_MINOR) + "." +
           std::to_string(ESP_NOW_MIDI_VERSION_PATCH);
}
#endif