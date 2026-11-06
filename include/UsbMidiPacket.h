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

/**
 * @brief Like writeUsbMidiSysEx(), but waits while the USB buffer is full.
 * @param wait Called when a packet does not fit; sleeps briefly and returns
 * `true` to try again, `false` to give up. Its argument is `true` while
 * ending a cut-off SysEx.
 *
 * Giving up in the middle of a message would leave the host waiting for `F7`
 * and ignoring the following MIDI, so it still tries to end the SysEx with `F7`.
 * @return `true` when the whole SysEx was written.
 */
template <typename Write, typename Wait>
bool writeUsbMidiSysExWaiting(const uint8_t *data, uint16_t length, Write write, Wait wait)
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
    while (!write(packet))
    {
      if (!wait(false))
      {
        if (i > 0)
        {
          const uint8_t end[4] = {0x05, 0xF7, 0, 0}; // SysEx ends with 1 byte
          while (!write(end) && wait(true))
          {
          }
        }
        return false;
      }
    }
  }
  return true;
}

/**
 * @brief Writes SysEx to USB, waiting up to a time budget for buffer space.
 *
 * A burst of SysEx (e.g. a GET_CONFIG reply) is larger than TinyUSB's MIDI
 * buffer, so writes must wait for the host to read. After a SysEx timed out,
 * the next ones within the back-off fail fast, so a host that stopped reading
 * does not stall the caller on every message.
 */
class UsbSysExPacer
{
public:
  static constexpr uint32_t BUDGET_MS = 20;   ///< Wait per SysEx for buffer space.
  static constexpr uint32_t END_EXTRA_MS = 10; ///< Extra wait to end a cut-off SysEx.
  static constexpr uint32_t BACKOFF_MS = 500;  ///< Fail fast this long after a timeout.

  /**
   * @param write Writes one packet, `false` when the buffer is full.
   * @param ready `false` when USB is gone (do not wait at all).
   * @param now Current time in milliseconds.
   * @param sleep Waits about 1 ms so the USB task can send.
   */
  template <typename Write, typename Ready, typename Now, typename Sleep>
  bool write(const uint8_t *data, uint16_t length, Write write, Ready ready, Now now, Sleep sleep)
  {
    const uint32_t start = now();
    const bool failFast = _stalled && start - _stalledAt < BACKOFF_MS;
    const bool ok = writeUsbMidiSysExWaiting(data, length, write, [&](bool ending)
                                             {
      if (failFast || !ready())
        return false;
      if (now() - start >= BUDGET_MS + (ending ? END_EXTRA_MS : 0))
        return false;
      sleep();
      return true; });
    _stalled = !ok;
    if (!ok)
    {
      _stalledAt = now();
    }
    return ok;
  }

private:
  bool _stalled = false;
  uint32_t _stalledAt = 0;
};

} // namespace enomik
