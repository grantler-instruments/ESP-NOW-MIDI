# Wi-Fi and ESP-IDF

The core transport (`esp_now_midi`) needs Wi-Fi in station (STA) mode before
ESP-NOW can run. Bring-up is handled by `esp_now_midi_wifi.h` with two
compile-time backends, and can be skipped when the application owns Wi-Fi.

## Backends

| Build | Backend |
|-------|---------|
| Arduino (including Arduino-ESP32) | `WiFi.mode(WIFI_STA)` / `WiFi.disconnect()` |
| Pure ESP-IDF (`ESP_PLATFORM` without `ARDUINO`) | `nvs_flash` + `esp_netif` + `esp_wifi` STA start |
| Host / native tests | No-op |

## `manageWifi`

`esp_now_midi::begin()` takes an optional fourth argument:

```cpp
bool begin(bool reducePowerAtCostOfLatency = false,
           bool autoPeerDiscovery = true,
           DataSentCallback callback = DefaultOnDataSent,
           bool manageWifi = true);
```

| `manageWifi` | Behavior |
|--------------|----------|
| `true` (default) | Library brings up Wi-Fi STA via the active backend, then starts ESP-NOW |
| `false` | Application must already have started Wi-Fi; library only configures ESP-NOW, channel (`ESP_NOW_MIDI_CHANNEL`), and power |

Example when Wi-Fi is managed outside:

```cpp
// ... your esp_wifi / WiFi setup ...
esp_now_midi transport;
transport.begin(/*reducePower*/ false, /*autoPeerDiscovery*/ true,
                esp_now_midi::DefaultOnDataSent, /*manageWifi*/ false);
```

Peers must operate on the **same radio channel**. When `manageWifi` is
`false` and you are joined to an AP, ESP-NOW follows that channel; override
`ESP_NOW_MIDI_CHANNEL` and your app’s channel configuration so they match.

## Using as an ESP-IDF component

This repository can be added as an IDF component (`CMakeLists.txt` +
`idf_component.yml`). Register dependency on `esp_wifi`, `esp_netif`, and
`nvs_flash`. Include `esp_now_midi.h` from your `main` (or another component).

Arduino sketches keep using the Library Manager / `libraries` folder layout
via `library.properties`; they do not use the IDF `CMakeLists.txt`.

Higher-level helpers (`enomik::Client`, `enomik::Dongle`, and related I/O) are
Arduino-oriented today and are not part of the IDF core component surface yet;
they are planned to be ported to ESP-IDF as well.
