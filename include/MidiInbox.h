#pragma once

#include <cstdint>
#include <cstring>

#include "./esp_now_midi_helpers.h"
#include "./esp_now_midi_lock.h"

#ifndef MIDI_INBOX_SIZE
#define MIDI_INBOX_SIZE 64
#endif

/**
 * @brief Hands received MIDI from the ESP-NOW receive callback (WiFi task) to
 * the loop task.
 *
 * push() only copies a few bytes under a critical section, so it is safe in the
 * callback. pop() is called from loop(). When full, new messages are dropped
 * and counted.
 */
class MidiInbox
{
public:
  bool push(const midi_message &msg, const uint8_t mac[6])
  {
    EspNowMidiLock lock(_mux);
    const uint16_t next = (_head + 1) % MIDI_INBOX_SIZE;
    if (next == _tail)
    {
      ++_dropped;
      return false;
    }
    _items[_head].msg = msg;
    if (mac)
    {
      memcpy(_items[_head].mac, mac, 6);
    }
    else
    {
      memset(_items[_head].mac, 0, 6);
    }
    _head = next;
    return true;
  }

  bool pop(midi_message &msg, uint8_t mac[6])
  {
    EspNowMidiLock lock(_mux);
    if (_tail == _head)
    {
      return false;
    }
    msg = _items[_tail].msg;
    memcpy(mac, _items[_tail].mac, 6);
    _tail = (_tail + 1) % MIDI_INBOX_SIZE;
    return true;
  }

  /** @return Messages dropped because the inbox was full (since boot). */
  uint32_t droppedCount() const
  {
    EspNowMidiLock lock(_mux);
    return _dropped;
  }

private:
  struct Entry
  {
    midi_message msg;
    uint8_t mac[6];
  };

  mutable portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
  Entry _items[MIDI_INBOX_SIZE];
  uint16_t _head = 0;
  uint16_t _tail = 0;
  uint32_t _dropped = 0;
};
