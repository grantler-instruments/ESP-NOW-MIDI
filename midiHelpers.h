#pragma once
#include <cstdint>

enum MidiStatus
{
    MIDI_UNKNOWN = 0x00,
    // channel voice messages
    MIDI_NOTE_OFF = 0x80,
    MIDI_NOTE_ON = 0x90,
    MIDI_CONTROL_CHANGE = 0xB0,
    MIDI_PROGRAM_CHANGE = 0xC0,
    MIDI_PITCH_BEND = 0xE0,
    MIDI_AFTERTOUCH = 0xD0,      // aka channel pressure
    MIDI_POLY_AFTERTOUCH = 0xA0, // aka key pressure
    // system messages
    MIDI_SYSEX = 0xF0,
    MIDI_TIME_CODE = 0xF1,
    MIDI_SONG_POS_POINTER = 0xF2,
    MIDI_SONG_SELECT = 0xF3,
    MIDI_TUNE_REQUEST = 0xF6,
    SYSEX_END = 0xF7,
    MIDI_TIME_CLOCK = 0xF8,
    MIDI_START = 0xFA,
    MIDI_CONTINUE = 0xFB,
    MIDI_STOP = 0xFC,
    MIDI_ACTIVE_SENSING = 0xFE,
    MIDI_SYSTEM_RESET = 0xFF
};
#define MIDI_MIN_BEND 0
#define MIDI_MAX_BEND 16383

// MPE
#define MIDI_RPN_MSB 0x65
#define MIDI_RPN_LSB 0x64
#define MIDI_DATA_ENTRY_MSB 0x06
#define MPE_CONFIGURATION_RPN_MSB 0x00
#define MPE_CONFIGURATION_RPN_LSB 0x06

// Original struct for internal use (1-based channels, separate fields)
struct midi_message
{
    uint8_t channel; // 1-16 (user-facing)
    MidiStatus status;
    uint8_t firstByte;
    uint8_t secondByte;
} __attribute__((packed));

// 3-byte packet for transmission (follows MIDI spec exactly)
struct midi_message_packet
{
    uint8_t statusByte; // Status + channel (0-15) combined
    uint8_t data1;
    uint8_t data2;

    // Convert from midi_message (1-based channel) to packet (0-based)
    static midi_message_packet fromMessage(const midi_message &msg)
    {
        midi_message_packet packet;

        // Combine status and channel (convert 1-based to 0-based)
        uint8_t channelZeroBased = (msg.channel - 1) & 0x0F;

        // System messages (0xF0-0xFF) don't use channel bits
        if (msg.status >= 0xF0)
        {
            packet.statusByte = msg.status;
        }
        else
        {
            packet.statusByte = msg.status | channelZeroBased;
        }

        packet.data1 = msg.firstByte;
        packet.data2 = msg.secondByte;

        return packet;
    }

    // Convert from packet (0-based channel) to midi_message (1-based)
    midi_message toMessage() const
    {
        midi_message msg;

        // Extract status and channel
        if (statusByte >= 0xF0)
        {
            // System message - no channel
            msg.status = (MidiStatus)statusByte;
            msg.channel = 0;
        }
        else
        {
            // Channel message - extract both
            msg.status = (MidiStatus)(statusByte & 0xF0);
            msg.channel = (statusByte & 0x0F) + 1; // Convert 0-based to 1-based
        }

        msg.firstByte = data1;
        msg.secondByte = data2;

        return msg;
    }

    // Helper to get the size of actual MIDI data
    uint8_t getDataSize() const
    {
        uint8_t status = statusByte & 0xF0;

        // System messages
        if (statusByte >= 0xF0)
        {
            switch (statusByte)
            {
            case MIDI_TIME_CODE:
            case MIDI_SONG_SELECT:
                return 2; // status + 1 data byte
            case MIDI_SONG_POS_POINTER:
                return 3; // status + 2 data bytes
            case MIDI_TUNE_REQUEST:
            case MIDI_TIME_CLOCK:
            case MIDI_START:
            case MIDI_CONTINUE:
            case MIDI_STOP:
            case MIDI_ACTIVE_SENSING:
            case MIDI_SYSTEM_RESET:
                return 1; // status only
            default:
                return 3; // default to full size
            }
        }

        // Channel messages
        switch (status)
        {
        case MIDI_PROGRAM_CHANGE:
        case MIDI_AFTERTOUCH:
            return 2; // status + 1 data byte
        default:
            return 3; // status + 2 data bytes
        }
    }
} __attribute__((packed));

struct midi_sysex_message
{
    uint8_t data[128];
    uint8_t length;
} __attribute__((packed));

struct midi_mpe_message
{
    uint8_t note;
    uint8_t velocity;
    uint8_t channel;      // Which channel this note is on
    int16_t pitchBend; // -8192 to +8191
    uint8_t pressure;     // 0-127 (channel aftertouch)
    uint8_t timbre;       // 0-127 (CC74)
    uint8_t slide;        // 0-127 (CC73) - optional Y-axis control
    bool active;

    midi_mpe_message() : note(0), velocity(0), channel(0), pitchBend(0),
                         pressure(0), timbre(64), slide(64), active(false) {}

    midi_mpe_message(uint8_t n, uint8_t v, uint8_t ch) : note(n), velocity(v), channel(ch),
                                                pitchBend(0), pressure(0), timbre(64),
                                                slide(64), active(true) {}
};