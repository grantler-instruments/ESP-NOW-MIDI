#pragma once
#define ESP_NOW_MIDI_VERSION_MAJOR 0
#define ESP_NOW_MIDI_VERSION_MINOR 10
#define ESP_NOW_MIDI_VERSION_PATCH 9


#define ENOMIK_STR_HELPER(x) #x
#define ENOMIK_STR(x) ENOMIK_STR_HELPER(x)
#define ESP_NOW_MIDI_VERSION_STR \
    ENOMIK_STR(ESP_NOW_MIDI_VERSION_MAJOR) "." \
    ENOMIK_STR(ESP_NOW_MIDI_VERSION_MINOR) "." \
    ENOMIK_STR(ESP_NOW_MIDI_VERSION_PATCH)


inline const char* getVersion() {
    return ESP_NOW_MIDI_VERSION_STR;
}

#ifdef ARDUINO
#include <Arduino.h>
inline String getVersionString() {
    return String(getVersion());
}
#endif