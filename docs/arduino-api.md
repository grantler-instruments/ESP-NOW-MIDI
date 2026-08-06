# C++ Arduino API overview

This section documents the Arduino / C++ public API (Arduino-ESP32). The Class,
File, and Namespace pages are generated from Doxygen comments in the library
headers. For pure ESP-IDF, see [C++ ESP-IDF](idf-api.md).

## Entry points

There are three main ways to use the library:

- [`esp_now_midi`](api/Classes/classesp__now__midi.md): low-level ESP-NOW MIDI
  transport. Initialize it, add peers, send MIDI messages, and register receive
  handlers.
- [`enomik::Client`](api/Classes/classenomik_1_1_client.md): higher-level client
  that wraps ESP-NOW MIDI setup, peer storage, and a MIDI SysEx configuration
  interface for pin mapping and board setup.
- `enomik::Dongle`: USB MIDI ↔ ESP-NOW bridge for a host-connected board
  (ESP32-S2/S3). Call `begin()` / `loop()`. Optional status UI via
  `enomik::Dongle::Display` and `setDisplay()`. Optional
  `setToHostFilter` / `setFromHostFilter` to drop or remap bridged messages.

For most application sketches that should integrate with the Enomik tools, start
with `enomik::Client`. Use `enomik::Dongle` for the USB host bridge. Use plain
`esp_now_midi` when you want direct control of send and receive.

## Supporting APIs

- [`esp_now_midi_helpers.h`](api/Files/esp__now__midi__helpers_8h/): MIDI message
  types, constants, and helpers such as pitch-bend conversion.
- [`EspNowMidiLog`](logging.md) (`esp_now_midi_log.h`): internal printf-style
  logger (`e` / `w` / `i` / `d`) with Arduino `Serial` and ESP-IDF `ESP_LOG*`
  backends. Debug is gated by `ESP_NOW_DEBUGGING`; see [Logging](logging.md).
- Wi-Fi bring-up (`esp_now_midi_wifi.h`) and `begin(..., manageWifi)`: see
  [C++ ESP-IDF → Wi-Fi](idf-api.md#wi-fi) (same API on Arduino).
- [`enomik::IO`](api/Classes/classenomik_1_1_i_o.md): pin and MIDI mapping
  helpers used by the Enomik client.
- [`enomik::PeerStorage`](api/Classes/classenomik_1_1_peer_storage.md):
  persistent peer storage for the client.
- [`enomik::SysExHandler`](api/Classes/classenomik_1_1_sys_ex_handler.md) and
  related SysEx encoder/decoder types: configuration protocol used over MIDI
  SysEx.

## Browse the reference

- [Classes](api/Classes/index.md)
- [Files](api/Files/index.md)
- [Namespaces](api/Namespaces/index.md)
