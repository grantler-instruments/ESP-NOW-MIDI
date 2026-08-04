# Getting started

ESP-NOW MIDI uses two roles: a USB-connected **dongle** and one or more
wireless **remote ESP boards**. The dongle bridges the wireless ESP-NOW MIDI
network to the wired USB MIDI world. It typically connects to a computer,
hardware synthesizer, mobile phone, or any other device that supports USB MIDI.

## Set up the dongle

1. Flash the [`dongle`](https://github.com/grantler-instruments/ESP-NOW-MIDI/tree/main/examples/dongle)
   example to a native USB-capable board, such as an ESP32-S2 Mini or an
   ESP32-S3 board. Both the S2 and S3 can act as USB MIDI devices. The sketch
   uses `enomik::Dongle` (`begin()` / `loop()`).
2. Find its Wi-Fi STA MAC address with the
   [`print_mac`](https://github.com/grantler-instruments/ESP-NOW-MIDI/tree/main/examples/print_mac)
   example, or from the dongle display.

If the dongle has an OLED display, set `HAS_DISPLAY` to `1` in
`examples/dongle/config.h`. To use a different panel or layout, subclass
`enomik::Dongle::Display`, implement `begin()` / `update()`, and register it
with `setDisplay()` before `begin()`.

Once the dongle is configured and flashed, you should not need to touch its
code again unless a library update requires new firmware.

## Connect a remote board

There are two ways to connect a remote board: use the higher-level
`enomik::Client` path for managed wireless setup and configurable I/O, or use
the plain `esp_now_midi` path for direct control of MIDI communication.

### `enomik::Client`

Use the `client` example or add `enomik::Client` to a sketch. Call `begin()`
from `setup()` and `loop()` from the sketch `loop()`. Pair with the dongle
using `addPeer()` or `addPeerFromString()` and its MAC address. Saved peers are
restored after reboot. The client handles the wireless setup internally and
provides a MIDI SysEx interface for board configuration that is compatible
with the [Enomik web tools](https://enomik.grantler-instruments.com/).

### Plain `esp_now_midi`

See `plain_echo` and related examples. Initialize the library, register the
dongle with `addPeer()`, and send or receive MIDI through `esp_now_midi`.
