#pragma once
#define ESP_NOW_MIDI_VERSION_MAJOR 0
#define ESP_NOW_MIDI_VERSION_MINOR 12
#define ESP_NOW_MIDI_VERSION_PATCH 3

/**
 * @brief Returns the library version as a semantic-version string.
 *
 * The returned value is assembled from the
 * `ESP_NOW_MIDI_VERSION_MAJOR`, `ESP_NOW_MIDI_VERSION_MINOR`, and
 * `ESP_NOW_MIDI_VERSION_PATCH` macros.
 *
 * @return The version in `major.minor.patch` format.
 */
inline String getVersion() {
    return String(ESP_NOW_MIDI_VERSION_MAJOR) + "." + 
           String(ESP_NOW_MIDI_VERSION_MINOR) + "." + 
           String(ESP_NOW_MIDI_VERSION_PATCH);
}