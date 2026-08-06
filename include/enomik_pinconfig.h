#pragma once
#include "./esp_now_midi_helpers.h"

/**
 * @file enomik_pinconfig.h
 * @brief Pin-to-MIDI mapping configuration.
 */

namespace enomik {

/**
 * @brief Maps an input pin to a MIDI message.
 */
struct PinConfig
{
    /** GPIO pin number. */
    uint8_t pin;
    /** ENOMIK input mode. */
    uint8_t mode;
    /** Trigger threshold for modes that support one. */
    uint8_t threshold = 0;
    /** MIDI channel, from 1 to 16. */
    uint8_t midi_channel = 1;
    /** MIDI message type to send. */
    MidiStatus midi_type = MidiStatus::MIDI_CONTROL_CHANGE;
    /** Control-change number when @ref midi_type is MIDI_CONTROL_CHANGE. */
    uint8_t midi_cc = 0;
    /** Note number when @ref midi_type is a note message. */
    uint8_t midi_note = 0;
    /** Lowest MIDI value to send. */
    uint8_t min_midi_value = 0;
    /** Highest MIDI value to send. */
    uint8_t max_midi_value = 127;

    /**
     * @brief Creates a pin configuration.
     * @param p GPIO pin number.
     * @param m ENOMIK input mode.
     */
    PinConfig(uint8_t p, uint8_t m)
        : pin(p), mode(m) {}
};

} // namespace enomik
