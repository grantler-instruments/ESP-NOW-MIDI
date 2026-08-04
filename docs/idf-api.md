# C++ ESP-IDF API overview

Use the **core** transport from a pure ESP-IDF project (no Arduino core).
Higher-level Enomik helpers (`enomik::Client`, `enomik::Dongle`, and related
I/O) are Arduino-oriented today and are not part of this surface yet; they are
planned to be ported to ESP-IDF as well.

## Entry point

- [`esp_now_midi`](api/Classes/classesp__now__midi.md) — send/receive MIDI over
  ESP-NOW. Call `begin()` once, `addPeer()`, then the `send*` / `setHandle*`
  APIs.

Add this repository as an IDF component (`CMakeLists.txt` /
`idf_component.yml`). Include `esp_now_midi.h` from your app.

## Supporting headers

| Header | Role |
|--------|------|
| [`midiHelpers.h`](api/Files/midi_helpers_8h.md) | Packet types and MIDI helpers (framework-agnostic) |
| `esp_now_midi_log.h` | `EspNowMidiLog` → `ESP_LOG*` on IDF; see [Logging](logging.md) |
| `esp_now_midi_wifi.h` | STA bring-up when `manageWifi` is true; see [Wi-Fi and ESP-IDF](wifi.md) |
| [`version.h`](api/Files/version_8h.md) | Semantic version macros / `getVersion()` |

## Wi-Fi ownership

```cpp
esp_now_midi transport;

// Library brings up STA (default):
transport.begin();

// Or you own Wi-Fi already:
transport.begin(false, true, esp_now_midi::DefaultOnDataSent, /*manageWifi*/ false);
```

Details and channel caveats: [Wi-Fi and ESP-IDF](wifi.md).

## Browse generated reference

The Doxygen pages cover the shared C++ headers (including Arduino-only Enomik
types). For IDF builds, stick to the core types above.

- [Classes](api/Classes/index.md)
- [Files](api/Files/index.md)
- [Namespaces](api/Namespaces/index.md) — includes `esp_now_midi_wifi`
