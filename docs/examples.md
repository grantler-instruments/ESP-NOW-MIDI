# Examples

The repository's [`examples/`](https://github.com/grantler-instruments/ESP-NOW-MIDI/tree/main/examples)
directory contains sketches for common ESP-NOW MIDI configurations.

## Core examples

- **`print_mac`**: prints the board's Wi-Fi MAC address.
- **`dongle`**: USB MIDI ↔ ESP-NOW bridge via `enomik::Dongle`. Optional OLED
  through a `Dongle::Display` subclass (`SSD1306Display` in the example).
- **`plain_echo`**: receives ESP-NOW MIDI without the Enomik helpers.
- **`client`**: a SysEx-configurable Enomik client.
- **`client_echo`**: echoes incoming MIDI messages.
- **`client_hello_midi`**: periodically sends MIDI over ESP-NOW.
- **`client_clocked`**: reacts to MIDI Start / Stop / Continue / Clock and song
  position / select over ESP-NOW.

## Hardware and protocol examples

- **`client_buttons`**: turns button presses into Note On and Note Off events.
- **`client_servo`**: controls a servo using MIDI control changes.
- **`din_midi_bridge`**: bridges five-pin DIN MIDI and ESP-NOW.
- **`client_dmx`**: controls DMX fixtures wirelessly.
- **`client_neopixels`**: drives a WS2812 strip from Note On/Off (pitch → LED
  index, channel → RGB component).
- **`client_mpu6500`**: reads an MPU6500 IMU and sends accelerometer / gyro data
  as MIDI control changes.
- **`client_vl53l0x`**: reads a VL53L0X time-of-flight sensor and sends distance
  as MIDI control changes.
- **`client_waveshare-esp32-s3-relay-6ch`**: note-driven relay controller for the
  Waveshare ESP32-S3 6-channel relay board (e.g. solenoids).

## Audio examples

- **`client_audio_m16_i2s`**: MIDI-controlled synthesizer using M16 and I2S.
- **`client_audio_audiotools_i2s`**: MIDI-controlled sine-wave synth using
  arduino-audio-tools and I2S.
- **`client_audio_pd`**: MIDI-controlled Pure Data sine synth using ESPdLib
  and I2S (`[notein]` / `Pd.noteOn`).

## Testing

- **`client_test`**: deterministic device-under-test for the interactive
  [test wizard](https://github.com/grantler-instruments/ESP-NOW-MIDI/blob/main/scripts/wizard/README.md).
  Not a product sketch, behaviour is fixed so the host harness can verify each step over MIDI.

Check each sketch's source for board requirements and optional library
dependencies.
