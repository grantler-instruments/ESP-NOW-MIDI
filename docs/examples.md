# Examples

The repository's [`examples/`](https://github.com/grantler-instruments/ESP-NOW-MIDI/tree/main/examples)
directory contains sketches for common ESP-NOW MIDI configurations.

## Core examples

- **`print_mac`**: prints the board's Wi-Fi MAC address.
- **`dongle`**: bridges USB MIDI and ESP-NOW MIDI.
- **`plain_echo`**: receives ESP-NOW MIDI without the Enomik helpers.
- **`client`**: a SysEx-configurable Enomik client.
- **`client_echo`**: echoes incoming MIDI messages.
- **`client_hello_midi`**: periodically sends MIDI over ESP-NOW.

## Hardware and protocol examples

- **`client_buttons`**: turns button presses into Note On and Note Off events.
- **`client_servo`**: controls a servo using MIDI control changes.
- **`din_midi_bridge`**: bridges five-pin DIN MIDI and ESP-NOW.
- **`client_dmx`**: controls DMX fixtures wirelessly.

## Audio examples

- **`client_audio_m16_i2s`**: MIDI-controlled synthesizer using M16 and I2S.
- **`client_audio_audiotools_i2s`**: MIDI-controlled sine-wave synth using
  arduino-audio-tools and I2S.

Check each sketch's source for board requirements and optional library
dependencies.
