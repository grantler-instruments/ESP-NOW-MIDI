/*
 * Minimal DMX512 sender for ESP32 with RS-485 transceiver (e.g. Grove DMX512 / SN75176).
 * No external DMX library. Uses 250k baud, 8N2; break + MAB then start code + 512 slots.
 */
#ifndef DMX_SENDER_H
#define DMX_SENDER_H

#include <Arduino.h>

#if !defined(ESP32)
#error "DMXSender.h is for ESP32 only (uses uart_set_break)."
#endif

#include "driver/uart.h"

class DMXSender {
 public:
  DMXSender() = default;

  // Start DMX: serial must be 250000 8N2 (call serial.begin(250000, SERIAL_8N2) before this).
  // dePin = GPIO for DE/RE (driver enable); high = transmit. Use same pin as SparkFun "enPin".
  // uartNum = UART used by serial (e.g. UART_NUM_2 for Serial2).
  void begin(HardwareSerial& serial, uint8_t dePin, uart_port_t uartNum, uint16_t numChannels = 512) {
    _serial = &serial;
    _dePin = dePin;
    _uartNum = uartNum;
    _numChannels = (numChannels <= 512) ? numChannels : 512;
    pinMode(_dePin, OUTPUT);
    digitalWrite(_dePin, LOW);
    memset(_channels, 0, sizeof(_channels));
  }

  // Set one channel (1-based, 1..512). No transmit until update().
  void writeByte(uint8_t value, uint16_t channel) {
    if (channel >= 1 && channel <= _numChannels) {
      _channels[channel - 1] = value;
    }
  }

  // Send one DMX frame: break, MAB, start code 0x00, then 512 channel bytes.
  void update() {
    if (!_serial) return;
    digitalWrite(_dePin, HIGH);
    delayMicroseconds(2);  // DE setup time

    uart_set_break(_uartNum, true);
    delayMicroseconds(DMX_BREAK_US);
    uart_set_break(_uartNum, false);
    delayMicroseconds(DMX_MAB_US);

    _serial->write((uint8_t)0x00);  // start code
    _serial->write(_channels, _numChannels);
    _serial->flush();

    digitalWrite(_dePin, LOW);
  }

 private:
  static const unsigned int DMX_BREAK_US = 100;  // break >= 88 µs
  static const unsigned int DMX_MAB_US = 12;    // MAB >= 8 µs

  HardwareSerial* _serial = nullptr;
  uint8_t _dePin = 0;
  uart_port_t _uartNum = UART_NUM_2;
  uint16_t _numChannels = 512;
  uint8_t _channels[512] = {};
};

#endif
