#pragma once

#include <cstdint>

#ifdef ARDUINO
#include <Arduino.h>
#elif defined(ESP_PLATFORM)
#include "freertos/FreeRTOS.h"
#include "esp_timer.h"
#else
// Host / native tests: single-threaded no-op critical sections.
#ifndef portMUX_INITIALIZER_UNLOCKED
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(mux) ((void)(mux))
#define portEXIT_CRITICAL(mux) ((void)(mux))
#endif
#endif
#include "./esp_now_midi_helpers.h"

#ifndef USB_MIDI_QUEUE_SIZE
#define USB_MIDI_QUEUE_SIZE 64
#endif

/**
 * Default age (ms) after which a queued message counts as stale and may be
 * dropped instead of being delivered late. 0 disables stale dropping.
 */
#ifndef USB_MIDI_STALE_MS
#define USB_MIDI_STALE_MS 500
#endif

namespace enomik {

/**
 * @brief ISR-safe ring buffer for MIDI messages destined for USB MIDI OUT.
 *
 * Clock messages can be coalesced via enqueueClock() so a burst of clocks
 * does not flood the queue.
 *
 * Every entry carries its enqueue time. When the host stops reading for a
 * while, dropStale() removes old entries so the host gets current data
 * instead of a delayed backlog once it reads again. Stale dropping never
 * loses information the host needs to end up in the right state:
 * - release messages (Note Off, Note On vel 0, pedal off, channel mode
 *   messages, Stop) are always kept,
 * - state messages (CC, Pitch Bend, Channel Pressure, Program Change, Song
 *   Position, Song Select) keep their latest value per channel/controller,
 * - only superseded state and momentary events (Note On, Poly Pressure,
 *   Start/Continue, Clock) are removed.
 *
 * When the queue is full, the oldest droppable entry is evicted so the newest
 * data gets in; release messages are never evicted.
 */
class UsbMidiQueue {
public:
  /** @return true for messages that end something (must never be dropped). */
  static bool isRelease(const midi_message &msg) {
    switch (msg.status) {
    case MIDI_NOTE_OFF:
    case MIDI_STOP:
    case MIDI_SYSTEM_RESET:
      return true;
    case MIDI_NOTE_ON:
      return msg.secondByte == 0;
    case MIDI_CONTROL_CHANGE:
      // Sustain / sostenuto / soft pedal released.
      if ((msg.firstByte == 64 || msg.firstByte == 66 || msg.firstByte == 67) &&
          msg.secondByte < 64) {
        return true;
      }
      // Channel mode messages (All Sound Off, Reset Controllers, All Notes Off...).
      return msg.firstByte >= 120;
    default:
      return false;
    }
  }

  /** @return true for messages whose latest value represents current state. */
  static bool isState(const midi_message &msg) {
    switch (msg.status) {
    case MIDI_CONTROL_CHANGE:
    case MIDI_PITCH_BEND:
    case MIDI_AFTERTOUCH:
    case MIDI_PROGRAM_CHANGE:
    case MIDI_SONG_POS_POINTER:
    case MIDI_SONG_SELECT:
      return true;
    default:
      return false;
    }
  }

  /** @return true when @p b carries a newer value for the same state as @p a. */
  static bool sameState(const midi_message &a, const midi_message &b) {
    if (a.status != b.status) {
      return false;
    }
    switch (a.status) {
    case MIDI_CONTROL_CHANGE:
      return a.channel == b.channel && a.firstByte == b.firstByte;
    case MIDI_PITCH_BEND:
    case MIDI_AFTERTOUCH:
    case MIDI_PROGRAM_CHANGE:
      return a.channel == b.channel;
    case MIDI_SONG_POS_POINTER:
    case MIDI_SONG_SELECT:
      return true;
    default:
      return false;
    }
  }

  void enqueue(const midi_message &msg) { enqueue(msg, nowMs()); }

  void enqueue(const midi_message &msg, uint32_t now) {
    portENTER_CRITICAL(&_mux);
    if (sizeLocked() >= kCapacity) {
      // Full: evict the oldest non-release entry to make room for new data.
      // If everything queued is a release, keep them and drop the newcomer
      // (unless it is a release itself, in which case the oldest release
      // goes; this only happens with 63+ releases pending).
      if (!evictOldestDroppableLocked()) {
        if (!isRelease(msg)) {
          ++_overflowDrops;
          portEXIT_CRITICAL(&_mux);
          return;
        }
        _tail = (_tail + 1) % USB_MIDI_QUEUE_SIZE;
      }
      ++_overflowDrops;
    }
    _items[_head].msg = msg;
    _items[_head].timeMs = now;
    _head = (_head + 1) % USB_MIDI_QUEUE_SIZE;
    portEXIT_CRITICAL(&_mux);
  }

  void enqueueClock() { enqueueClock(nowMs()); }

  void enqueueClock(uint32_t now) {
    portENTER_CRITICAL(&_mux);
    _pendingClock = true;
    _clockTimeMs = now;
    portEXIT_CRITICAL(&_mux);
  }

  bool hasPending() {
    portENTER_CRITICAL(&_mux);
    const bool pending = (_tail != _head) || _pendingClock;
    portEXIT_CRITICAL(&_mux);
    return pending;
  }

  uint16_t pendingCount() {
    portENTER_CRITICAL(&_mux);
    uint16_t count = sizeLocked();
    if (_pendingClock) {
      ++count;
    }
    portEXIT_CRITICAL(&_mux);
    return count;
  }

  bool peek(midi_message &msg) {
    portENTER_CRITICAL(&_mux);
    if (_tail != _head) {
      msg = _items[_tail].msg;
      portEXIT_CRITICAL(&_mux);
      return true;
    }
    if (_pendingClock) {
      msg.status = MIDI_TIME_CLOCK;
      msg.channel = 0;
      msg.firstByte = 0;
      msg.secondByte = 0;
      portEXIT_CRITICAL(&_mux);
      return true;
    }
    portEXIT_CRITICAL(&_mux);
    return false;
  }

  void consumeHead() {
    portENTER_CRITICAL(&_mux);
    if (_tail != _head) {
      _tail = (_tail + 1) % USB_MIDI_QUEUE_SIZE;
    } else if (_pendingClock) {
      _pendingClock = false;
    }
    portEXIT_CRITICAL(&_mux);
  }

  /** @brief Drops stale entries using the default clock. See dropStale(uint32_t, uint32_t). */
  uint16_t dropStale(uint32_t maxAgeMs) { return dropStale(nowMs(), maxAgeMs); }

  /**
   * @brief Removes entries older than @p maxAgeMs (see class docs for rules).
   *
   * Cheap when nothing is stale: only the oldest entry is checked.
   *
   * @param now Current time in milliseconds.
   * @param maxAgeMs Maximum age; 0 disables dropping.
   * @return Number of entries removed (including a stale coalesced clock).
   */
  uint16_t dropStale(uint32_t now, uint32_t maxAgeMs) {
    if (maxAgeMs == 0) {
      return 0;
    }
    uint16_t dropped = 0;
    portENTER_CRITICAL(&_mux);
    if (_pendingClock && (uint32_t)(now - _clockTimeMs) > maxAgeMs) {
      _pendingClock = false;
      ++dropped;
    }
    // Entries are in enqueue order, so if the oldest is fresh, all are.
    if (_tail != _head && (uint32_t)(now - _items[_tail].timeMs) > maxAgeMs) {
      dropped += compactStaleLocked(now, maxAgeMs);
    }
    _staleDrops += dropped;
    portEXIT_CRITICAL(&_mux);
    return dropped;
  }

  /**
   * @brief Marks every queued entry as enqueued at @p now.
   *
   * Used after a USB recovery, during which aging was paused, so messages that
   * waited for the host are not dropped as stale the moment it is back.
   */
  void refreshTimestamps(uint32_t now) {
    portENTER_CRITICAL(&_mux);
    const uint16_t n = sizeLocked();
    for (uint16_t k = 0; k < n; ++k) {
      _items[at(k)].timeMs = now;
    }
    if (_pendingClock) {
      _clockTimeMs = now;
    }
    portEXIT_CRITICAL(&_mux);
  }

  void clear() {
    portENTER_CRITICAL(&_mux);
    _head = 0;
    _tail = 0;
    _pendingClock = false;
    portEXIT_CRITICAL(&_mux);
  }

  /** @return Total entries dropped because they were stale (since boot). */
  uint32_t staleDropCount() {
    portENTER_CRITICAL(&_mux);
    const uint32_t n = _staleDrops;
    portEXIT_CRITICAL(&_mux);
    return n;
  }

  /** @return Total entries dropped because the queue was full (since boot). */
  uint32_t overflowDropCount() {
    portENTER_CRITICAL(&_mux);
    const uint32_t n = _overflowDrops;
    portEXIT_CRITICAL(&_mux);
    return n;
  }

private:
  struct Entry {
    midi_message msg;
    uint32_t timeMs;
  };

  // One slot stays free to tell full from empty.
  static constexpr uint16_t kCapacity = USB_MIDI_QUEUE_SIZE - 1;

  static uint32_t nowMs() {
#ifdef ARDUINO
    return millis();
#elif defined(ESP_PLATFORM)
    return (uint32_t)(esp_timer_get_time() / 1000);
#else
    return 0;
#endif
  }

  uint16_t sizeLocked() const {
    return (_head >= _tail) ? (_head - _tail)
                            : (USB_MIDI_QUEUE_SIZE - _tail + _head);
  }

  uint16_t at(uint16_t offset) const {
    return (_tail + offset) % USB_MIDI_QUEUE_SIZE;
  }

  /** Removes the logical entry at @p offset, keeping order. */
  void removeAtLocked(uint16_t offset) {
    const uint16_t n = sizeLocked();
    for (uint16_t k = offset; k + 1 < n; ++k) {
      _items[at(k)] = _items[at(k + 1)];
    }
    _head = (_head + USB_MIDI_QUEUE_SIZE - 1) % USB_MIDI_QUEUE_SIZE;
  }

  bool evictOldestDroppableLocked() {
    const uint16_t n = sizeLocked();
    for (uint16_t k = 0; k < n; ++k) {
      if (!isRelease(_items[at(k)].msg)) {
        removeAtLocked(k);
        return true;
      }
    }
    return false;
  }

  /** @return true when an entry after @p offset supersedes it. */
  bool hasNewerStateLocked(uint16_t offset, uint16_t n) const {
    const midi_message &m = _items[at(offset)].msg;
    for (uint16_t k = offset + 1; k < n; ++k) {
      if (sameState(m, _items[at(k)].msg)) {
        return true;
      }
    }
    return false;
  }

  uint16_t compactStaleLocked(uint32_t now, uint32_t maxAgeMs) {
    const uint16_t n = sizeLocked();
    uint16_t write = 0;
    uint16_t dropped = 0;
    for (uint16_t read = 0; read < n; ++read) {
      const Entry &e = _items[at(read)];
      bool keep = true;
      if ((uint32_t)(now - e.timeMs) > maxAgeMs && !isRelease(e.msg)) {
        // Stale: keep only the latest value of a state message.
        keep = isState(e.msg) && !hasNewerStateLocked(read, n);
      }
      if (keep) {
        if (write != read) {
          _items[at(write)] = e;
        }
        ++write;
      } else {
        ++dropped;
      }
    }
    _head = at(write);
    return dropped;
  }

  portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
  Entry _items[USB_MIDI_QUEUE_SIZE];
  volatile uint16_t _head = 0;
  volatile uint16_t _tail = 0;
  volatile bool _pendingClock = false;
  uint32_t _clockTimeMs = 0;
  uint32_t _staleDrops = 0;
  uint32_t _overflowDrops = 0;
};

} // namespace enomik
