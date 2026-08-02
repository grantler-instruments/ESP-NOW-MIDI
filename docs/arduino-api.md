# Arduino C++ API overview

This section documents the Arduino / C++ public API. The Class, File, and
Namespace pages are generated from Doxygen comments in the library headers.

## Entry points

There are two main ways to use the library:

- [`esp_now_midi`](api/Classes/classesp__now__midi.md): low-level ESP-NOW MIDI
  transport. Initialize it, add peers, send MIDI messages, and register receive
  handlers.
- [`enomik::Client`](api/Classes/classenomik_1_1_client.md): higher-level client
  that wraps ESP-NOW MIDI setup, peer storage, and a MIDI SysEx configuration
  interface for pin mapping and board setup.

For most application sketches that should integrate with the Enomik tools, start
with `enomik::Client`. Use plain `esp_now_midi` when you want direct control of
send and receive.

## Supporting APIs

- [`midiHelpers.h`](api/Files/midi_helpers_8h.md): MIDI message types, constants,
  and helpers such as pitch-bend conversion.
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
