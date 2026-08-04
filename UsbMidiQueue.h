#pragma once

#include <Arduino.h>
#include "esp_now_midi_helpers.h"

#ifndef USB_MIDI_QUEUE_SIZE
#define USB_MIDI_QUEUE_SIZE 64
#endif

/**
 * @brief ISR-safe ring buffer for MIDI messages destined for USB MIDI OUT.
 *
 * Clock messages can be coalesced via enqueueClock() so a burst of clocks
 * does not flood the queue.
 */
class UsbMidiQueue {
public:
  void enqueue(const midi_message &msg) {
    portENTER_CRITICAL(&_mux);
    const uint16_t next = (_head + 1) % USB_MIDI_QUEUE_SIZE;
    if (next != _tail) {
      _items[_head] = msg;
      _head = next;
    }
    portEXIT_CRITICAL(&_mux);
  }

  void enqueueClock() {
    portENTER_CRITICAL(&_mux);
    _pendingClock = true;
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
    uint16_t count = (_head >= _tail)
                         ? (_head - _tail)
                         : (USB_MIDI_QUEUE_SIZE - _tail + _head);
    if (_pendingClock) {
      ++count;
    }
    portEXIT_CRITICAL(&_mux);
    return count;
  }

  bool peek(midi_message &msg) {
    portENTER_CRITICAL(&_mux);
    if (_tail != _head) {
      msg = _items[_tail];
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

  void clear() {
    portENTER_CRITICAL(&_mux);
    _head = 0;
    _tail = 0;
    _pendingClock = false;
    portEXIT_CRITICAL(&_mux);
  }

private:
  portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
  midi_message _items[USB_MIDI_QUEUE_SIZE];
  volatile uint16_t _head = 0;
  volatile uint16_t _tail = 0;
  volatile bool _pendingClock = false;
};
