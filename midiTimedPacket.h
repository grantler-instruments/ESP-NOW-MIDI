#pragma once

#include "midiHelpers.h"
#include <cstddef>
#include <cstdint>
#include <cstring>

/** First byte of a timed ESP-NOW MIDI frame (not a MIDI status). */
#ifndef ESP_NOW_MIDI_TIMED_MAGIC
#define ESP_NOW_MIDI_TIMED_MAGIC 0x00
#endif

/** Sender tick unit in microseconds (uint16 on the wire). */
#ifndef ESP_NOW_MIDI_TICK_US
#define ESP_NOW_MIDI_TICK_US 100
#endif

/** Default playout cushion T in milliseconds. Override before including headers. */
#ifndef ESP_NOW_MIDI_JITTER_BUFFER_MS
#define ESP_NOW_MIDI_JITTER_BUFFER_MS 8
#endif

/** Per-peer timed-message ring capacity. */
#ifndef ESP_NOW_MIDI_JITTER_BUFFER_SIZE
#define ESP_NOW_MIDI_JITTER_BUFFER_SIZE 32
#endif

/** Re-anchor when no timed RX from a peer for this many milliseconds. */
#ifndef ESP_NOW_MIDI_REANCHOR_GAP_MS
#define ESP_NOW_MIDI_REANCHOR_GAP_MS 3000
#endif

/** Re-anchor when implied wait exceeds this many milliseconds. */
#ifndef ESP_NOW_MIDI_ABSURD_OFFSET_MS
#define ESP_NOW_MIDI_ABSURD_OFFSET_MS 100
#endif

/** MIDI realtime statuses that bypass the jitter buffer and are always sent raw. */
inline bool isMidiRealtimeStatus(uint8_t statusByte)
{
  return statusByte == MIDI_TIME_CLOCK || statusByte == MIDI_START ||
         statusByte == MIDI_CONTINUE || statusByte == MIDI_STOP;
}

/**
 * @brief Timed ESP-NOW MIDI wire helpers.
 *
 * Layout: `[0x00][tick_lo][tick_hi][statusByte][data1?][data2?]`
 * `tick` is little-endian uint16 in units of `ESP_NOW_MIDI_TICK_US`.
 */
struct midi_timed_packet
{
  static constexpr size_t kHeaderSize = 3; // magic + tick16
  static constexpr size_t kMinSize = 4;    // header + 1-byte MIDI
  static constexpr size_t kMaxSize = 6;    // header + 3-byte MIDI

  static bool isTimedFrame(const uint8_t *data, int len)
  {
    return data != nullptr && len >= static_cast<int>(kMinSize) &&
           len <= static_cast<int>(kMaxSize) && data[0] == ESP_NOW_MIDI_TIMED_MAGIC;
  }

  /**
   * @brief Parses a timed frame into tick + MIDI packet.
   * @return `false` when magic/length/status/size are inconsistent.
   */
  static bool parse(const uint8_t *data, int len, uint16_t &tick, midi_message_packet &midi)
  {
    if (!isTimedFrame(data, len))
    {
      return false;
    }

    tick = static_cast<uint16_t>(data[1] | (static_cast<uint16_t>(data[2]) << 8));

    const int midiLen = len - static_cast<int>(kHeaderSize);
    if (midiLen < 1 || data[3] < 0x80)
    {
      return false;
    }

    std::memset(&midi, 0, sizeof(midi));
    std::memcpy(&midi, data + kHeaderSize, static_cast<size_t>(midiLen));

    if (static_cast<int>(midi.getDataSize()) != midiLen)
    {
      return false;
    }
    return true;
  }

  /**
   * @brief Packs MIDI into a timed frame.
   * @return Total bytes written (4–6), or 0 on failure.
   */
  static size_t pack(uint8_t *out, size_t outCap, uint16_t tick, const midi_message_packet &midi)
  {
    if (out == nullptr)
    {
      return 0;
    }
    const uint8_t midiLen = midi.getDataSize();
    const size_t total = kHeaderSize + midiLen;
    if (midiLen < 1 || total > outCap || total > kMaxSize)
    {
      return 0;
    }
    out[0] = ESP_NOW_MIDI_TIMED_MAGIC;
    out[1] = static_cast<uint8_t>(tick & 0xFF);
    out[2] = static_cast<uint8_t>((tick >> 8) & 0xFF);
    std::memcpy(out + kHeaderSize, &midi, midiLen);
    return total;
  }

  static uint16_t microsToTick(uint32_t microsSinceEpoch)
  {
    return static_cast<uint16_t>(microsSinceEpoch / ESP_NOW_MIDI_TICK_US);
  }

  static uint32_t tickDeltaToMicros(uint16_t tickDelta)
  {
    return static_cast<uint32_t>(tickDelta) * ESP_NOW_MIDI_TICK_US;
  }
};
