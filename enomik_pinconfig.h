#pragma once
#include "./midiHelpers.h"

namespace enomik {
    static constexpr uint8_t ENOMIK_INPUT         = 0x00;
    static constexpr uint8_t ENOMIK_OUTPUT        = 0x01;
    static constexpr uint8_t ENOMIK_INPUT_PULLUP  = 0x02;
    static constexpr uint8_t ENOMIK_ANALOG_INPUT  = 0x03;
    static constexpr uint8_t ENOMIK_ANALOG_OUTPUT = 0x04;
    static constexpr uint8_t ENOMIK_INPUT_TOUCH   = 0x05;
}

struct PinConfig
{
    uint8_t pin;
    uint8_t mode;
    uint8_t threshold = 0;
    uint8_t midi_channel = 1;
    MidiStatus midi_type = MidiStatus::MIDI_CONTROL_CHANGE;
    uint8_t midi_cc = 0;
    uint8_t midi_note = 0;
    uint8_t min_midi_value = 0;
    uint8_t max_midi_value = 127;

    PinConfig(uint8_t p, uint8_t m)
        : pin(p), mode(m) {}
};