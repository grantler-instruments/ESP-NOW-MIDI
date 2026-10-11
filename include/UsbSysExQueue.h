#pragma once

#ifdef ARDUINO
#include <Arduino.h>
#elif defined(ESP_PLATFORM)
#include "freertos/FreeRTOS.h"
#endif
#include "./esp_now_midi_sysex.h"
#include <cstring>

#ifndef USB_SYSEX_QUEUE_SLOTS
#define USB_SYSEX_QUEUE_SLOTS 2
#endif

namespace enomik
{

/**
 * @brief ISR-safe ring of complete SysEx messages for deferred USB MIDI OUT.
 *
 * ESP-NOW receive callbacks enqueue here; Dongle::loop() drains to TinyUSB.
 */
class UsbSysExQueue
{
public:
    /** @param from Sender MAC to keep with the message, or nullptr. */
    bool enqueue(const uint8_t *data, uint16_t length, const uint8_t *from = nullptr)
    {
        if (data == nullptr || length == 0 || length > esp_now_midi_sysex::MAX_MESSAGE)
            return false;

        portENTER_CRITICAL(&_mux);
        const uint8_t next = static_cast<uint8_t>((_head + 1) % USB_SYSEX_QUEUE_SLOTS);
        if (next == _tail)
        {
            portEXIT_CRITICAL(&_mux);
            return false; // full
        }
        Slot &slot = _slots[_head];
        memcpy(slot.data, data, length);
        slot.length = length;
        slot.hasFrom = from != nullptr;
        if (from)
            memcpy(slot.from, from, 6);
        _head = next;
        portEXIT_CRITICAL(&_mux);
        return true;
    }

    bool hasPending()
    {
        portENTER_CRITICAL(&_mux);
        const bool pending = _tail != _head;
        portEXIT_CRITICAL(&_mux);
        return pending;
    }

    bool peek(const uint8_t *&data, uint16_t &length)
    {
        const uint8_t *from = nullptr;
        return peek(data, length, from);
    }

    /** @param from Set to the sender MAC, or nullptr when none was queued. */
    bool peek(const uint8_t *&data, uint16_t &length, const uint8_t *&from)
    {
        portENTER_CRITICAL(&_mux);
        if (_tail == _head)
        {
            portEXIT_CRITICAL(&_mux);
            return false;
        }
        data = _slots[_tail].data;
        length = _slots[_tail].length;
        from = _slots[_tail].hasFrom ? _slots[_tail].from : nullptr;
        portEXIT_CRITICAL(&_mux);
        return true;
    }

    void consumeHead()
    {
        portENTER_CRITICAL(&_mux);
        if (_tail != _head)
            _tail = static_cast<uint8_t>((_tail + 1) % USB_SYSEX_QUEUE_SLOTS);
        portEXIT_CRITICAL(&_mux);
    }

    void clear()
    {
        portENTER_CRITICAL(&_mux);
        _head = 0;
        _tail = 0;
        portEXIT_CRITICAL(&_mux);
    }

private:
    struct Slot
    {
        uint8_t data[esp_now_midi_sysex::MAX_MESSAGE];
        uint16_t length;
        uint8_t from[6];
        bool hasFrom;
    };

    portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
    Slot _slots[USB_SYSEX_QUEUE_SLOTS];
    volatile uint8_t _head = 0;
    volatile uint8_t _tail = 0;
};

} // namespace enomik
