# ESP-NOW-MIDI
This is an Arduino library for sending and receiving MIDI messages via the ESP-NOW protocol.
A typical setup requires two ESP-NOW capable boards, where the board connected to your computer needs to be MIDI-capable 

## Documentation

The full guide is published at [grantler-instruments.github.io/ESP-NOW-MIDI](https://grantler-instruments.github.io/ESP-NOW-MIDI/).

The ESP32-S2 Mini (Lolin S2 Mini) can act as both a receiver and a sender. An S3 should also work as a receiver.
Any ESP board with Wi-Fi capabilities should work as a sender.

## Use it basically everywhere
* the dongle shows up as a class compliant midi controller
* use it with MAX, pd, any DAW, processing, openFrameworks, any game engine that supports MIDI, ... or even in the browser or mobile phone

## Compatibility
* This library only works with ESP microcontrollers
* It has been developed using a LOLIN S2 MINI
* It should work with S3s as well, in the long run, S3 will be the main target microcontroller, due to its dual core architecture
* When uploading to an S3, please make sure you have USB mode: USB-OTG (TinyUSB) activated

## Features
* **enomik::Client I/O** Add `enomik::Client` to your sketch and it becomes a MIDI SysEx–configurable client: map pins to MIDI without extra wiring logic, e.g. digital input 3 → MIDI CC
* **enomik::Client MIDI wrapper** Helper that takes care of the ESP-NOW setup, provides a common interface for USB and ESP-NOW MIDI
* **examples**
  * **print_mac**: periodically prints the mac address to the serial monitor
  * **dongle**: this is your esp now midi interface to your computer or any other usb midi host. it converts esp now message to midi messages, requires a midi capable board, e.g. esp32 s2 mini.
  the config.h you can disable the display - in case you are not using one
  if you wanna put them into a case, you can probably find 3d models online, here is one for an esp32 s2 mini: https://www.thingiverse.com/thing:5427531
  * **plain_echo**: same as client_echo, but without the enomik helpers
  * **client**: fully configurable client that works with enomik boards and the enomik webapp
  * **client_echo**: simply echos the incoming MIDI messages
  * **client_hello_midi**: simple demo firmware that periodically sends midi messages via esp now
  * **client_dac_i2s (wip)** - synth that can be controlled via dongle, e.g. send midi notes from a DAW to the dongle midi device - ** this is currently broken **
  * **client_waveshare-esp32-s3-relay-6ch** - simple relay controller that listens to note on/off messages, e.g. control solenoids 
  * **client_buttons** - reads button press/release and sends note on/off accordingly
  * **client_dmx** - control your dmx fixtures wirelessly
    * CC MSB/LSB mapping
      * MIDI Ch 1, CC 0 (MSB) + CC 32 (LSB) -> DMX Channel 1
      * MIDI Ch 1, CC 1 (MSB) + CC 33 (LSB) -> DMX Channel 2
      * MIDI Ch 1, CC 31 (MSB) + CC 63 (LSB) -> DMX Channel 32
      * MIDI Ch 2, CC 0 (MSB) + CC 32 (LSB) -> DMX Channel 33
      * MIDI Ch 16, CC 31 (MSB) + CC 63 (LSB) -> DMX Channel 512
    * NOTEON direct mapping
      * MIDI Ch 1, NOTE 0, VEL: 127 -> DMX Channel 1, value: 127*2
      * MIDI Ch 4, NOTE 127, VEL: 127 -> DMX Channel 512, value: 127*2
  * **client_servo** - control a servo via MIDI CC using [ESP32Servo](https://github.com/madhephaestus/ESP32Servo/)
  * **client_audio_m16_i2s** - MIDI-controlled synthesizer using the [M16](https://github.com/algomusic/M16) audio library, outputting audio over I2S (e.g. MAX98357A). Receives Note On/Off and plays a sine wave with an ADSR envelope
  * **client_audio_audiotools_i2s** - MIDI-controlled sine wave synth using [arduino-audio-tools](https://github.com/pschatzmann/arduino-audio-tools), streaming audio over I2S. Receives Note On/Off and adjusts frequency and amplitude accordingly
  * **din_midi_bridge** - bidirectional DIN MIDI ↔ ESP-NOW bridge: forwards incoming serial MIDI (5-pin DIN) over ESP-NOW, and plays back received ESP-NOW MIDI to a DIN MIDI OUT port

## Breaking changes (this library is under active development => please make sure you are using the latest version)
* This repo uses Semantic Versioning, although strict adherence will only come into effect at version 1.0.0.
* Starting with version 0.10.0 the esp_now_midi setup was renamed to begin, power saving has been disabled in flavor for lower latencies (can be enabled by setting begin(reducePowerAtCostOfLatency=true)
* Starting with version 0.9.0, packages are sent as 3 byte messages (channel+status combined, as the MIDI specs), older version have used 4 bytes
* Starting with version 0.6, this library requires ESP32 board version greater or equal than 3.3.0 


## Usage

You usually run **two roles**: a **dongle** (USB MIDI + ESP-NOW on one board, e.g. ESP32-S2 Mini with TinyUSB) and one or more **remote ESPs** that speak ESP-NOW MIDI.

### 1. Set up the dongle

1. Flash the **dongle** example onto your USB-capable board.
2. Note the dongle’s Wi-Fi STA MAC: run **print_mac** on that board (serial), or read it from the dongle display if you use one.

If the dongle has an OLED, set `HAS_DISPLAY` to `1` in `examples/dongle/config.h`.

### 2. Connect a remote board

**Option A — `enomik::Client` (SysEx / web app)**  
Use the **client** example or add `enomik::Client` to your sketch: call `begin()` in `setup()` and call the client’s `loop()` from your sketch `loop()`. Pair once with the dongle using `addPeer()` / `addPeerFromString()` with the dongle MAC (see the other client examples), or pair from the host via MIDI SysEx; saved peers are restored on boot. Configure pins and routing over SysEx or with the [enomik web app](https://grantler-instruments.github.io/enomik-app/).

**Option B — Plain `esp_now_midi`**  
See **plain_echo** and similar: initialize the library, `addPeer()` with the dongle MAC, then send and receive MIDI with the same style of API as a typical Arduino MIDI library.

### 3. Computer → remote ESP

USB MIDI sent to the dongle is forwarded over ESP-NOW. For the first link-up, the remote side typically needs the dongle registered as a peer (`addPeer` or SysEx pairing as above); the dongle learns the client when the client sends ESP-NOW MIDI traffic.

### Pitch bend

Use the **signed** API everywhere unless you already have raw wire bytes:

| API | Value range | Center (no bend) |
|-----|-------------|------------------|
| `sendPitchBend(value, channel)` | −8192 … +8191 | **0** |
| `setHandlePitchBend(...)` callback `value` | −8192 … +8191 | **0** |
| `sendPitchBendRaw(value, channel)` | 0 … 16383 (wire) | **8192** |

`sendPitchBend()` and receive callbacks match the [FortySevenEffects MIDI library](https://github.com/FortySevenEffects/arduino_midi_library) convention. Pass handler values through unchanged — do not add or subtract 8192.

### enomik 3000
Drop `enomik::Client` into your firmware and your board turns into a MIDI SysEx–configurable device: routing, pin modes, and basic I/O can be set from a host over SysEx instead of hard-coding every mapping. This library is integrated with the [ESP-NOW MIDI Kit](https://grantler-instruments.github.io/enomik-app/) — the no-code app for creating (wireless) MIDI devices.

### Circuit Python
A circuit python version is in the making as well. Contributions here are very welcome.

## Benchmarks
This repository includes benchmark data and their analysis (see `benchmarks/` and `scripts/`).

Each `*_analysis.txt` contains these latency metrics:

* `samples`: Number of numeric latency samples parsed from the benchmark file.
* `min` / `max`: Fastest and slowest observed sample.
* `mean`: Arithmetic average latency.
* `median`: 50th percentile (middle value), usually more robust than mean when outliers exist.
* `p95`: 95th percentile (95% of samples are at or below this value).
* `p99`: 99th percentile (tail-latency indicator).
* `sigma_sample`: Sample standard deviation (`stdev`), spread estimate for a sample set.
* `sigma_population`: Population standard deviation (`pstdev`), spread estimate if treating the file as the full population.
* `p25`: 25th percentile.
* `p75`: 75th percentile.
* `iqr`: Interquartile range (`p75 - p25`), spread of the middle 50% of values.
* `mad`: Median absolute deviation (median of `abs(x - median)`), robust jitter metric.
* `peak_to_peak`: Total observed range (`max - min`), very sensitive to outliers.

Practical reading:
* For "typical" latency, look at `median`.
* For bad-case behavior, look at `p95` and `p99`.
* For jitter/stability, compare `mad`, `iqr`, and `sigma_*`.


## SysEx configuration protocol

Pin configuration, peers, MAC, reset, and version query use MIDI SysEx. Full specification:

**[doc/enomik-sysex-protocol.md](doc/enomik-sysex-protocol.md)**

## Release candidates

Before tagging a release, run the interactive **test wizard** against real hardware. CI covers unit tests and example builds; it does **not** replace this MIDI integration check.

Flash `examples/client_test` (and `examples/dongle` for the wireless phase), then:

```bash
cd scripts/wizard
./run.sh
```

See **[scripts/wizard/README.md](scripts/wizard/README.md)** for phases, hardware pins, and flags.

## Dependencies
* dependencies for the library should be automatically installed
* examples/dongle additionally depends on
  * Adafruit GFX Library
  * Adafruit SSD1306
* examples/client_dac_i2s depends on mozzi
* examples/client_dmx uses Grove DMX512 (SN75176) with a built-in minimal sender (no extra library). Optional: use **luksal/ESP32-DMX** or **pierrejay/esp32-EZDMX** instead (see comment in the sketch). 
* examples/client_servo depends on **ESP32Servo**
* examples/client_audio_m16_i2s depends on [M16](https://github.com/algomusic/M16)
* examples/client_audio_audiotools_i2s depends on [arduino-audio-tools](https://github.com/pschatzmann/arduino-audio-tools)
* examples/din_midi_bridge depends on [Arduino MIDI Library](https://github.com/FortySevenEffects/arduino_midi_library) by FortySevenEffects

## Contributing
If you find any bugs feel free to submit an issue on github, also PRs are very welcome.

## License
This library is licensed under the **GNU Lesser General Public License (LGPL) version 3**.

* You are free to **use, modify, and distribute** this library, including in commercial products.
* If you **modify the library itself** and distribute it, you must make those modifications available under the same LGPL license.
* You **do not have to open-source your own code** that simply uses this library.

For the full license text, see the `LICENSE` file included with this library or visit: [https://www.gnu.org/licenses/lgpl-3.0.html](https://www.gnu.org/licenses/lgpl-3.0.html).

## Support

If you find this library useful, you can support development:

[![Buy Me A Coffee](https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png)](https://buymeacoffee.com/thomasgeissl)

