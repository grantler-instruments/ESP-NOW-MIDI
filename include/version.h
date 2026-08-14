#pragma once
#define ESP_NOW_MIDI_VERSION_MAJOR 0
#define ESP_NOW_MIDI_VERSION_MINOR 19
#define ESP_NOW_MIDI_VERSION_PATCH 0

/**
 * @brief Returns the library version as a semantic-version string.
 *
 * The returned value is assembled from the
 * `ESP_NOW_MIDI_VERSION_MAJOR`, `ESP_NOW_MIDI_VERSION_MINOR`, and
 * `ESP_NOW_MIDI_VERSION_PATCH` macros.
 *
 * @return The version in `major.minor.patch` format.
 */
#include "./esp_now_midi_compat.h"

#ifdef ARDUINO
inline PortableString getVersion() {
    return PortableString(ESP_NOW_MIDI_VERSION_MAJOR) + "." +
           PortableString(ESP_NOW_MIDI_VERSION_MINOR) + "." +
           PortableString(ESP_NOW_MIDI_VERSION_PATCH);
}
#else
inline PortableString getVersion() {
    return std::to_string(ESP_NOW_MIDI_VERSION_MAJOR) + "." +
           std::to_string(ESP_NOW_MIDI_VERSION_MINOR) + "." +
           std::to_string(ESP_NOW_MIDI_VERSION_PATCH);
}
#endif
