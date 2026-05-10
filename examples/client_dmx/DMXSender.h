/*
 * Minimal DMX512 sender for ESP32 with RS-485 transceiver (e.g. Grove DMX512 / SN75176).
 * No external DMX library. Uses 250k baud, 8N2; break + MAB then start code + 512 slots.
 *
 * Break generation uses a baud-rate trick (no IDF uart_set_break / uart_send_break needed),
 * so this compiles on all ESP32 variants (S2, S3, C3, …) and all IDF/arduino-esp32 versions.
 */
#ifndef DMX_SENDER_H
#define DMX_SENDER_H

#include <Arduino.h>

#if !defined(ESP32)
#error "DMXSender.h is for ESP32 only (Arduino-ESP32 core required)."
#endif

class DMXSender {
 public:
  DMXSender() = default;

  // Start DMX: serial must be 250000 8N2 (call serial.begin(250000, SERIAL_8N2) before this).
  // dePin = GPIO for DE/RE (driver enable); high = transmit.
  void begin(HardwareSerial& serial, uint8_t dePin, uint16_t numChannels = 512) {
    _serial = &serial;
    _dePin = dePin;
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

  // Send one DMX frame: break, MAB, start code 0x00, then numChannels bytes.
  void update() {
    if (!_serial) return;
    digitalWrite(_dePin, HIGH);
    delayMicroseconds(2);  // DE setup time

    // Break via baud-rate trick (works on all ESP32 variants, no IDF break API needed):
    // At 83333 baud, 0x00 with 8N2 = start bit + 8 zero bits low = 9 × 12 µs ≈ 108 µs (≥88 µs).
    // The 2 stop bits that follow provide ≈24 µs MAB (≥8 µs required).
    _serial->updateBaudRate(83333);
    _serial->write((uint8_t)0x00);
    _serial->flush();
    _serial->updateBaudRate(250000);

    _serial->write((uint8_t)0x00);  // start code
    _serial->write(_channels, _numChannels);
    _serial->flush();

    digitalWrite(_dePin, LOW);
  }

 private:
  HardwareSerial* _serial = nullptr;
  uint8_t _dePin = 0;
  uint16_t _numChannels = 512;
  uint8_t _channels[512] = {};
};

#endif
