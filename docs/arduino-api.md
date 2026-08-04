# Arduino C++ API overview

This section documents the Arduino / C++ public API. The Class, File, and
Namespace pages are generated from Doxygen comments in the library headers.

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

## Optional jitter buffer (0.16+)

ESP-NOW MIDI defaults to raw 1–3 byte packets with ASAP delivery. To trade a
small fixed delay for smoother timing (see [issue #31](https://github.com/grantler-instruments/ESP-NOW-MIDI/issues/31)):

```cpp
espNowMidi.setReduceJitterAtCostOfLatency(true); // or dongle/client wrapper
espNowMidi.setJitterBufferMs(8);                 // T; 0 = ASAP; default from ESP_NOW_MIDI_JITTER_BUFFER_MS
// Call espNowMidi.update() in loop() — Dongle/Client do this for you.
```

- `enomik::Dongle` turns jitter reduction **on by default**
  (`DONGLE_REDUCE_JITTER_AT_COST_OF_LATENCY`, override with `0` before includes).
  `enomik::Client` / plain `esp_now_midi` stay off until you enable them.
- Each side configures itself independently. If a peer sends timed packets but
  this device has jitter reduction **off**, timed frames are still accepted and
  released ASAP (no surprise latency).
- Realtime MIDI (`F8`/`FA`/`FB`/`FC`) is always sent raw and delivered ASAP.
- Override the default `T` before includes: `#define ESP_NOW_MIDI_JITTER_BUFFER_MS 8`.
- **Compat:** older firmware treats `len > 3` as SysEx and drops timed frames.
  Dongle-default timed TX needs remotes on 0.16+ (or disable the dongle default).

Wire layout: `[0x00][tick16 LE ×100µs][midi 1–3 bytes]`.

## Supporting APIs

- [`midiHelpers.h`](api/Files/midi_helpers_8h.md): MIDI message types, constants,
  and helpers such as pitch-bend conversion.
- `midiTimedPacket.h` / `MidiJitterBuffer.h`: timed wire format and per-peer
  playout ring used by `esp_now_midi`.
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
