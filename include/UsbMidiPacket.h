#pragma once

#include <cstdint>

#include "./esp_now_midi_helpers.h"

namespace enomik {

/**
 * @brief Builds the 4-byte USB-MIDI event packet (cable 0) for @p msg.
 *
 * Writing whole packets means a busy USB buffer drops a message cleanly
 * instead of leaving a partial one behind.
 *
 * @return `false` for messages without a packet here (SysEx, time code,
 * channel outside 1-16).
 */
inline bool toUsbMidiPacket(const midi_message &msg, uint8_t packet[4])
{
  const uint8_t status = msg.status;
  const uint8_t d1 = msg.firstByte & 0x7F;
  const uint8_t d2 = msg.secondByte & 0x7F;
  switch (msg.status)
  {
  case MIDI_NOTE_OFF:
  case MIDI_NOTE_ON:
  case MIDI_POLY_AFTERTOUCH:
  case MIDI_CONTROL_CHANGE:
  case MIDI_PROGRAM_CHANGE:
  case MIDI_AFTERTOUCH:
  case MIDI_PITCH_BEND:
  {
    if (msg.channel < 1 || msg.channel > 16)
    {
      return false;
    }
    const bool oneDataByte = msg.status == MIDI_PROGRAM_CHANGE || msg.status == MIDI_AFTERTOUCH;
    packet[0] = status >> 4;
    packet[1] = status | (msg.channel - 1);
    packet[2] = d1;
    packet[3] = oneDataByte ? 0 : d2;
    return true;
  }
  case MIDI_SONG_POS_POINTER:
    packet[0] = 0x03;
    packet[1] = status;
    packet[2] = d1;
    packet[3] = d2;
    return true;
  case MIDI_SONG_SELECT:
    packet[0] = 0x02;
    packet[1] = status;
    packet[2] = d1;
    packet[3] = 0;
    return true;
  case MIDI_TIME_CLOCK:
  case MIDI_START:
  case MIDI_CONTINUE:
  case MIDI_STOP:
    packet[0] = 0x0F;
    packet[1] = status;
    packet[2] = 0;
    packet[3] = 0;
    return true;
  default:
    return false;
  }
}

/**
 * @brief Writes a complete SysEx (`F0` ... `F7`) as USB-MIDI packets.
 * @param write Called with each packet; returning `false` (USB busy) stops.
 * @return `true` when every packet was written.
 */
template <typename Write>
bool writeUsbMidiSysEx(const uint8_t *data, uint16_t length, Write write)
{
  if (!data || length < 2 || data[0] != 0xF0 || data[length - 1] != 0xF7)
  {
    return false;
  }
  for (uint16_t i = 0; i < length; i += 3)
  {
    const uint8_t n = (length - i) < 3 ? (length - i) : 3;
    const bool last = i + n == length;
    const uint8_t packet[4] = {static_cast<uint8_t>(last ? 0x04 + n : 0x04),
                               data[i],
                               n > 1 ? data[i + 1] : uint8_t(0),
                               n > 2 ? data[i + 2] : uint8_t(0)};
    if (!write(packet))
    {
      return false;
    }
  }
  return true;
}

} // namespace enomik
