# ESP-NOW-MIDI

Library for sending and receiving MIDI messages via the ESP-NOW protocol.
A typical setup uses two ESP-NOW capable boards: a USB MIDI **dongle** connected to your computer (or other USB MIDI host), and one or more remote ESP boards. The dongle shows up as a class-compliant MIDI device, use it with any DAW, Max, pd, Processing, openFrameworks, game engines that support MIDI, or even in the browser or on a phone.

## Documentation

Full guide: [grantler-instruments.github.io/ESP-NOW-MIDI](https://grantler-instruments.github.io/ESP-NOW-MIDI/)

- [Getting started](https://grantler-instruments.github.io/ESP-NOW-MIDI/getting-started/)
- [Examples](https://grantler-instruments.github.io/ESP-NOW-MIDI/examples/)
- [API reference](https://grantler-instruments.github.io/ESP-NOW-MIDI/api-reference/)
- [SysEx protocol](https://grantler-instruments.github.io/ESP-NOW-MIDI/enomik-sysex-protocol/)
- [Benchmarks](https://grantler-instruments.github.io/ESP-NOW-MIDI/benchmarks/)

## Platforms

- **Arduino**: Arduino-ESP32 sketches under `examples/`
- **ESP-IDF**: pure IDF component + examples under `examples_idf/`
- **PlatformIO**: `library.json` + `platformio.ini` in each Arduino example
- **CircuitPython**: `esp_now_midi.py` (same packet format as the C++ library; largely untested, contributions welcome)

## Hardware

- **Dongle** (USB MIDI ↔ ESP-NOW): ESP32-S2 or ESP32-S3 with TinyUSB. Developed on a LOLIN S2 Mini; S3 is the longer-term main target (dual core). When uploading to an S3, set USB mode to USB-OTG (TinyUSB).
- **Remote / sender**: any ESP board with Wi-Fi.

## Features

- **`enomik::Client`**: SysEx-configurable MIDI client — map pins to MIDI (e.g. digital input → CC) without hard-coding every route. Works with the [enomik web app](https://grantler-instruments.github.io/enomik-app/).
- **MIDI wrapper**: ESP-NOW setup plus a common interface for USB and ESP-NOW MIDI.
- **`enomik::Dongle`**: USB MIDI ↔ ESP-NOW bridge for a host-connected board. Optional status display via `Dongle::Display` + `setDisplay()`.

## Quick start

You usually run **two roles**: a **dongle** (USB MIDI + ESP-NOW on one board) and one or more **remote ESPs**.

### 1. Set up the dongle

1. Flash the **dongle** example onto your USB-capable board (`enomik::Dongle` with `begin()` / `loop()`).
2. Note the dongle’s Wi-Fi STA MAC: run **print_mac** on that board (serial), or read it from the dongle display if you use one.

If the dongle has an OLED, set `HAS_DISPLAY` to `1` in `examples/dongle/config.h`. Custom UI: subclass `enomik::Dongle::Display` and call `setDisplay()` before `begin()`.

### 2. Connect a remote board

**Option A — `enomik::Client` (SysEx / web app)**  
Use the **client** example or add `enomik::Client` to your sketch: call `begin()` in `setup()` and the client’s `loop()` from your sketch `loop()`. Pair once with the dongle using `addPeer()` / `addPeerFromString()` with the dongle MAC, or pair from the host via MIDI SysEx; saved peers are restored on boot. Configure pins and routing over SysEx or with the [enomik web app](https://grantler-instruments.github.io/enomik-app/).

**Option B — Plain `esp_now_midi`**  
See **plain_echo** and similar: initialize the library, `addPeer()` with the dongle MAC, then send and receive MIDI with the same style of API as a typical Arduino MIDI library.

### 3. Computer → remote ESP

USB MIDI sent to the dongle is forwarded over ESP-NOW. For the first link-up, the remote side typically needs the dongle registered as a peer (`addPeer` or SysEx pairing); the dongle learns the client when the client sends ESP-NOW MIDI traffic.

More detail: [Getting started](https://grantler-instruments.github.io/ESP-NOW-MIDI/getting-started/).

## Examples

Catalog (core, hardware, audio, test): [Examples](https://grantler-instruments.github.io/ESP-NOW-MIDI/examples/).

### Arduino

Sketches under `examples/` — open in Arduino IDE or `arduino-cli`, or build with PlatformIO (below). Requires **Arduino-ESP32 3.3.0+**.

### ESP-IDF

Pure IDF ports under `examples_idf/` (`dongle`, `client`, `client_test`). From an example folder:

```bash
idf.py set-target esp32s2   # or esp32 / esp32s3
idf.py build
```

See each example’s README and the [C++ ESP-IDF](https://grantler-instruments.github.io/ESP-NOW-MIDI/idf-api/) guide.

### PlatformIO

Each Arduino example has a `platformio.ini`; shared board settings live in `platformio/common.ini`. Build artifacts go to `.pio/workspaces/` at the repo root (not under `examples/`), so Arduino IDE does not pick up dependency sketches from PlatformIO `libdeps`.

From an example folder (e.g. `examples/dongle`):

```bash
pio run
pio run -t upload
```

Build every example from the repo root:

```bash
bash scripts/platformio_build_examples.sh
```

Example configs use the [pioarduino `espressif32` platform](https://github.com/pioarduino/platform-espressif32) because official `platformio/espressif32` does not support Arduino core 3.x.

To use the library in your own PlatformIO project, add to `platformio.ini`:

```ini
[env:lolin_s2_mini]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.30/platform-espressif32.zip
board = lolin_s2_mini
framework = arduino

build_flags =
    -I ${PROJECT_PACKAGES_DIR}/framework-arduinoespressif32/libraries/Network/src

lib_deps =
    https://github.com/grantler-instruments/ESP-NOW-MIDI.git
    WiFi
    Networking
    Preferences
    EEPROM
    adafruit/Adafruit TinyUSB Library
    https://github.com/FortySevenEffects/arduino_midi_library.git
```

For ESP32-S3 boards with USB MIDI, also set:

```ini
board_build.usb_mode = tinyusb
```

## Dependencies

Library dependencies should install automatically. Extra libraries used by individual examples (display, servo, audio, DIN MIDI, etc.) are noted in each sketch and in the [examples guide](https://grantler-instruments.github.io/ESP-NOW-MIDI/examples/).

## Versioning

This repo uses Semantic Versioning. Strict adherence starts at 1.0.0; until then, APIs may change — use the latest release and see [CHANGELOG.md](https://github.com/grantler-instruments/ESP-NOW-MIDI/blob/main/CHANGELOG.md). Notable migration: since 0.10.0, `esp_now_midi` setup was renamed to `begin()`; power saving is off by default for lower latency (`begin(reducePowerAtCostOfLatency=true)` to re-enable).

## Contributing

Bugs and PRs are welcome on GitHub. Before tagging a release, run the interactive hardware test wizard (`examples/client_test` + `examples/dongle`); see [scripts/wizard/README.md](https://github.com/grantler-instruments/ESP-NOW-MIDI/blob/main/scripts/wizard/README.md).

## License

This library is licensed under the **GNU Lesser General Public License (LGPL) version 3**.

* You are free to **use, modify, and distribute** this library, including in commercial products.
* If you **modify the library itself** and distribute it, you must make those modifications available under the same LGPL license.
* You **do not have to open-source your own code** that simply uses this library.

For the full license text, see the `LICENSE` file included with this library or visit: [https://www.gnu.org/licenses/lgpl-3.0.html](https://www.gnu.org/licenses/lgpl-3.0.html).

## Support

If you find this library useful, you can support development:

[![Buy Me A Coffee](https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png)](https://buymeacoffee.com/thomasgeissl)
