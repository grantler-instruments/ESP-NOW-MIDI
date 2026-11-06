# CircuitPython API

The CircuitPython implementation uses the same ESP-NOW packet format as the
C++ core. For Arduino and ESP-IDF C++, see the [API reference overview](api-reference.md).

It needs CircuitPython's `espnow` and `wifi` modules (ESP32 boards).
MicroPython is not supported.

## Setup

Copy `esp_now_midi.py` to `CIRCUITPY/lib/`. `examples/hello_midi.py` is a
complete `code.py`.

## Radio channel

All devices must be on the same Wi-Fi channel (6 by default, like the C++
library). CircuitPython has no channel setter, so `begin(channel=6)` starts an
access point on that channel and stops it right away, which leaves the radio
there. Peers are added without a fixed channel and follow the radio.

If the board is connected to a Wi-Fi network, the radio has to stay on that
network's channel: `begin()` leaves it alone and prints a warning when it
differs. Either don't connect, or build the C++ devices with
`ESP_NOW_MIDI_CHANNEL` set to the network's channel.

## Polling

There is no receive callback. Call `read()` often in the main loop; it handles
every waiting packet without blocking and calls the handlers.

::: esp_now_midi
