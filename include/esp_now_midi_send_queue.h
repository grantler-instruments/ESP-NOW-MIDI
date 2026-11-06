/**
 * @file esp_now_midi_send_queue.h
 * @brief Outgoing ESP-NOW packets waiting for the sender task.
 *
 * Short MIDI messages (1-3 bytes) and SysEx transport frames wait in two
 * rings; a sequence number keeps them in the order they were queued. Not
 * thread-safe: esp_now_midi guards it with a critical section.
 *
 * When the ring for short messages fills up, the least important messages
 * are refused first (see Priority), so a note-off still gets a slot after
 * controller values stopped being accepted. A new value for a continuous
 * controller, pitch bend or aftertouch replaces one for the same peer that
 * is still waiting, so a moving knob cannot fill the queue with stale values.
 */
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace esp_now_midi_queue
{

/** @brief Which messages are refused first when the queue fills up. */
enum class Priority : uint8_t
{
  Low,    ///< Controller values, pitch bend, aftertouch, clock, active sensing.
  Normal, ///< Note on, program change, other system messages, SysEx frames.
  High,   ///< Note off, sustain-type switches, channel mode (all notes off), stop, reset.
};

// Controllers whose order matters (bank select, RPN/NRPN data entry) or
// whose every value matters (switches, channel mode): never replaced.
inline bool isOrderedController(uint8_t cc)
{
  return cc == 0 || cc == 32 || cc == 6 || cc == 38 || (cc >= 64 && cc <= 69) ||
         (cc >= 96 && cc <= 101) || cc >= 120;
}

/** @brief Priority of a packet: a short MIDI message, or a longer SysEx frame. */
inline Priority classify(const uint8_t *data, size_t len)
{
  if (!data || len == 0 || len > 3)
  {
    return Priority::Normal;
  }
  const uint8_t status = data[0];
  switch (status & 0xF0)
  {
  case 0x80:
    return Priority::High;
  case 0x90:
    return len >= 3 && data[2] == 0 ? Priority::High : Priority::Normal;
  case 0xA0:
  case 0xD0:
  case 0xE0:
    return Priority::Low;
  case 0xB0:
    if (len >= 2 && ((data[1] >= 64 && data[1] <= 69) || data[1] >= 120))
    {
      return Priority::High;
    }
    return len >= 2 && isOrderedController(data[1]) ? Priority::Normal : Priority::Low;
  default:
    break;
  }
  switch (status)
  {
  case 0xF8: // clock
  case 0xFE: // active sensing
    return Priority::Low;
  case 0xFC: // stop
  case 0xFF: // reset
    return Priority::High;
  default:
    return Priority::Normal;
  }
}

// Whether a waiting message @p queued can be replaced by @p data: same
// status byte and, for controllers and poly aftertouch, same number.
inline bool replaces(const uint8_t *data, size_t len, const uint8_t *queued, size_t queuedLen)
{
  if (len != queuedLen || len < 2 || data[0] != queued[0])
  {
    return false;
  }
  switch (data[0] & 0xF0)
  {
  case 0xD0:
  case 0xE0:
    return true;
  case 0xA0:
    return data[1] == queued[1];
  case 0xB0:
    return data[1] == queued[1] && !isOrderedController(data[1]);
  default:
    return false;
  }
}

/**
 * @brief Two rings of waiting packets, each with a destination MAC.
 * @tparam ShortSlots Slots for 1-3 byte MIDI messages.
 * @tparam FrameSlots Slots for longer packets (SysEx frames).
 * @tparam FrameBytes Largest packet a frame slot holds.
 */
template <size_t ShortSlots, size_t FrameSlots, size_t FrameBytes>
class SendQueue
{
  static_assert(ShortSlots >= 8 && FrameSlots >= 1, "queue too small");

public:
  enum class Result : uint8_t
  {
    Queued,
    Replaced, ///< Took the place of a waiting value (see replaces()).
    Full,
    TooLong,
  };

  /**
   * @brief Adds a packet for @p mac, or replaces a waiting value.
   * @param flags Caller's bits, returned by pop() (e.g. "this is a probe").
   */
  Result push(const uint8_t mac[6], const uint8_t *data, size_t len, uint8_t flags = 0)
  {
    if (!mac || !data || len == 0)
    {
      return Result::TooLong;
    }
    if (len <= 3)
    {
      return pushShort(mac, data, len, flags);
    }
    if (len > FrameBytes)
    {
      return Result::TooLong;
    }
    if (_frameCount >= FrameSlots)
    {
      return Result::Full;
    }
    FrameSlot &slot = _frames[(_frameHead + _frameCount) % FrameSlots];
    memcpy(slot.mac, mac, 6);
    memcpy(slot.data, data, len);
    slot.len = static_cast<uint16_t>(len);
    slot.flags = flags;
    slot.seq = _nextSeq++;
    ++_frameCount;
    return Result::Queued;
  }

  /**
   * @brief Takes the oldest packet.
   * @param out Buffer of at least FrameBytes.
   * @param flags The flags it was pushed with.
   * @return Its length, or 0 when the queue is empty.
   */
  size_t pop(uint8_t mac[6], uint8_t *out, uint8_t &flags)
  {
    const bool haveShort = _shortCount > 0;
    const bool haveFrame = _frameCount > 0;
    if (!haveShort && !haveFrame)
    {
      return 0;
    }
    const bool takeShort =
        haveShort && (!haveFrame || static_cast<int16_t>(_shorts[_shortHead].seq -
                                                         _frames[_frameHead].seq) < 0);
    if (takeShort)
    {
      const ShortSlot &slot = _shorts[_shortHead];
      memcpy(mac, slot.mac, 6);
      memcpy(out, slot.data, slot.len);
      flags = slot.flags;
      _shortHead = (_shortHead + 1) % ShortSlots;
      --_shortCount;
      return slot.len;
    }
    const FrameSlot &slot = _frames[_frameHead];
    memcpy(mac, slot.mac, 6);
    memcpy(out, slot.data, slot.len);
    flags = slot.flags;
    _frameHead = (_frameHead + 1) % FrameSlots;
    --_frameCount;
    return slot.len;
  }

  size_t size() const { return _shortCount + _frameCount; }
  bool empty() const { return size() == 0; }
  void clear() { _shortCount = _frameCount = 0; }

  /** @brief Waiting short messages above which a priority is refused. */
  static constexpr size_t limit(Priority p)
  {
    return p == Priority::Low      ? ShortSlots / 2
           : p == Priority::Normal ? ShortSlots - ShortSlots / 8
                                   : ShortSlots;
  }

private:
  struct ShortSlot
  {
    uint8_t mac[6];
    uint8_t data[3];
    uint8_t len;
    uint8_t flags;
    uint16_t seq;
  };
  struct FrameSlot
  {
    uint8_t mac[6];
    uint16_t len;
    uint8_t flags;
    uint16_t seq;
    uint8_t data[FrameBytes];
  };

  Result pushShort(const uint8_t mac[6], const uint8_t *data, size_t len, uint8_t flags)
  {
    const Priority priority = classify(data, len);
    if (priority == Priority::Low)
    {
      for (size_t i = 0; i < _shortCount; i++)
      {
        ShortSlot &slot = _shorts[(_shortHead + i) % ShortSlots];
        if (memcmp(slot.mac, mac, 6) == 0 && slot.flags == flags &&
            replaces(data, len, slot.data, slot.len))
        {
          memcpy(slot.data, data, len);
          return Result::Replaced;
        }
      }
    }
    if (_shortCount >= limit(priority))
    {
      return Result::Full;
    }
    ShortSlot &slot = _shorts[(_shortHead + _shortCount) % ShortSlots];
    memcpy(slot.mac, mac, 6);
    memcpy(slot.data, data, len);
    slot.len = static_cast<uint8_t>(len);
    slot.flags = flags;
    slot.seq = _nextSeq++;
    ++_shortCount;
    return Result::Queued;
  }

  ShortSlot _shorts[ShortSlots] = {};
  FrameSlot _frames[FrameSlots] = {};
  size_t _shortHead = 0;
  size_t _shortCount = 0;
  size_t _frameHead = 0;
  size_t _frameCount = 0;
  uint16_t _nextSeq = 0;
};

} // namespace esp_now_midi_queue
