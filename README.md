# ESP-NOW-MIDI
This is an Arduino library for sending and receiving MIDI messages via the ESP-NOW protocol.
A typical setup requires two ESP-NOW capable boards, where the board connected to your computer needs to be MIDI-capable 

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
* **enomik::Client I/O** MIDI sysex configuratable client: zero programming for basic I/O, e.g. digital input 3 -> MIDI CC
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

## Breaking changes (this library is under active development => please make sure you are using the latest version)
* This repo uses Semantic Versioning, although strict adherence will only come into effect at version 1.0.0.
* Starting with version 0.10.0 the esp_now_midi setup was renamed to begin, power saving has been disabled in flavor for lower latencies (can be enabled by setting begin(reducePowerAtCostOfLatency=true)
* Starting with version 0.9.0, packages are sent as 3 byte messages (channel+status combined, as the MIDI specs), older version have used 4 bytes
* Starting with version 0.6, this library requires ESP32 board version greater or equal than 3.3.0 


## Usage
1. Upload the print_mac example to an ESP32-S2 Mini board. The MAC address will be printed to via serial, or if you have a display connect to the dongle, you can skip this step, as the mac address will be printed on the display.
1. Upload the dongle example to an ESP32-S2 Mini board.
2. Copy the MAC address of the dongle and register it via the addPeer function
3. Integrate your sensor reading code into the sender.
4. Use the provided API to send MIDI messages, similar to the original MIDI library.
5. Or use the client with the no-code enomik web-app

If you are planning to send midi messages from your computer to your other esp32 micro controller, the dongle firmware will forward midi messages received via usb midi, the sender first needs to register its mac address by sending any midi message to the dongle via esp now midi.

If you are using the dongle with a display then make sure you have set `HAS_DISPLAY 1` in config.h 

### enomik 3000 (WIP)
This library is fully integrated in the [ESP-NOW MIDI Kit](https://grantler-instruments.github.io/enomik-app/) - the no-code app for creating (wireless) MIDI devices.

### Circuit Python (WIP)
A circuit python version is in the making as well. Contributions here are very welcome.

## Benchmarks
Round-trip path used for tests:

`pd --usb midi--> dongle --wireless midi--> client_echo --wireless midi--> dongle --usb midi--> pd`

Metrics are generated with:

`python3 scripts/analyze_benchmark.py <benchmark_file.txt>`

The script writes a sibling `<old_name>_analysis.txt` with:
`mean`, `median`, `p95`, `p99`.

Current benchmark summaries:

| Transport | File | Samples | Mean (ms) | Median (ms) | p95 (ms) | p99 (ms) |
|---|---|---:|---:|---:|---:|---:|
| BLE MIDI | `benchmarks/bleMidi/1m_0obs_m4_m1_osx.txt` | 999 | 22.347 | 17.142 | 48.752 | 49.660 |
| BLE MIDI | `benchmarks/bleMidi/3m_1obs_m4_m1_osx.txt` | 999 | 18.800 | 11.632 | 48.823 | 49.841 |
| BLE MIDI | `benchmarks/bleMidi/5m_2obs_m4_m1_osx.txt` | 999 | 20.079 | 12.607 | 48.344 | 49.706 |
| RTP MIDI | `benchmarks/rtpMidi/1m_0obs_m4_m1_osx.txt` | 999 | 26.461 | 25.759 | 46.430 | 49.342 |
| USB MIDI | `benchmarks/usbMidi/1m_m4_osx.txt` | 999 | 6.118 | 6.100 | 11.338 | 12.404 |


## sysex interface
### set pin config
1. start: 0xF0
1. manufacturer id: 0x7D
1. command id: 0x01 (set pin config), 0x09 (reset)
1. pin: any valid pin number
1. pin_mode: 0x00 (INPUT), 0x01 (OUTPUT), 0x02 (INPUT_PULLUP), 0x03(ANALOG_INPUT), 0x04(ANALOG_OUTPUT)
1. midi channel: 1-16
1. midi type: midi status byte divided by 2, e.g. CC (0xB0, 176) => (0x58, 88)
1. controller or note: e.g. 3C,60
1. midi min: 0
1. midi max: 1
1. end: 0xF7

* e.g. configure pin 3 to read digital values and send out CC: `0xF0, 0x7D, 0x01, 0x03, 0x02, 0x58, 0xF7`

### reset
1. start: 0xF0
1. manufacturer id: 0x7D
1. command id:  0x09 (reset)
1. end: 0xF7

* e.g. reset and clear pin configs: `0xF0, 0x7D, 0x09, 0xF7`

## Dependencies
* dependencies for the library should be automatically installed
* examples/dongle additionally depends on
  * Adafruit GFX Library
  * Adafruit SSD1306
* examples/client_dac_i2s depends on mozzi
* examples/client_dmx uses Grove DMX512 (SN75176) with a built-in minimal sender (no extra library). Optional: use **luksal/ESP32-DMX** or **pierrejay/esp32-EZDMX** instead (see comment in the sketch). 
* examples/client_servo depends on **ESP32Servo**

## Contributing
If you find any bugs feel free to submit an issue on github, also PRs are very welcome.


## License
This library is licensed under the **GNU Lesser General Public License (LGPL) version 3**.

* You are free to **use, modify, and distribute** this library, including in commercial products.
* If you **modify the library itself** and distribute it, you must make those modifications available under the same LGPL license.
* You **do not have to open-source your own code** that simply uses this library.

For the full license text, see the `LICENSE` file included with this library or visit: [https://www.gnu.org/licenses/lgpl-3.0.html](https://www.gnu.org/licenses/lgpl-3.0.html).

