# C++ ESP-IDF API overview

Use the library from a pure ESP-IDF project (no Arduino core).
Requires **ESP-IDF ≥ 5.5** (send callback uses `wifi_tx_info_t`).
CI and local builds use **ESP-IDF v6.0.1**.

The **core** transport (`esp_now_midi`) is the default IDF component surface
under `include/`. Higher-level Enomik helpers (`enomik::Client`,
`enomik::Dongle`, SysEx I/O) also build on IDF — see
[`examples_idf/`](https://github.com/grantler-instruments/ESP-NOW-MIDI/tree/main/examples_idf)
(`client`, `client_test`, `dongle`). Those examples wrap the repo-root Enomik
headers via a thin CMake component; they are not on the core component include
path by default.

## Entry point

- [`esp_now_midi`](api/Classes/classesp__now__midi.md) — send/receive MIDI over
  ESP-NOW. Call `begin()` once, `addPeer()`, then the `send*` / `setHandle*`
  APIs.

```cpp
#include "esp_now_midi.h"

esp_now_midi transport;

extern "C" void app_main(void)
{
    transport.begin();
    // transport.addPeer(mac);
    // transport.sendNoteOn(60, 100, 1);
}
```

## Install in an IDF project

The repository root **is** the IDF component (`CMakeLists.txt` +
`idf_component.yml`). Only headers under `include/` are on the component include
path (core transport + portable helpers: GPIO, prefs, USB MIDI, logging, Wi-Fi).
Repo-root Enomik headers (`enomik_client.h`, `enomik_dongle.h`, …) are **not**
on that path by default — pull them in the same way
[`examples_idf/`](https://github.com/grantler-instruments/ESP-NOW-MIDI/tree/main/examples_idf)
does (wrapper `INCLUDE_DIRS` pointing at the checkout root). The component
depends on `esp_wifi`, `esp_netif`, `nvs_flash`, and related IDF drivers.
Prefer naming the component directory `esp_now_midi` so
`REQUIRES esp_now_midi` stays clean.

This library is **not** published on the Espressif Component Registry yet; use
git, a submodule, or a local path.

### 1. Git submodule under `components/` (recommended)

```bash
cd your-idf-project
git submodule add https://github.com/grantler-instruments/ESP-NOW-MIDI.git \
  components/esp_now_midi
```

In `main/CMakeLists.txt` (or whichever component uses the API):

```cmake
idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_now_midi
)
```

Then build as usual:

```bash
. $IDF_PATH/export.sh
idf.py set-target esp32s3
idf.py build
```

### 2. Component Manager (git dependency)

In the project’s root `idf_component.yml` or `main/idf_component.yml`:

```yaml
dependencies:
  esp_now_midi:
    git: https://github.com/grantler-instruments/ESP-NOW-MIDI.git
    # version: "v0.16.0"   # optional tag / commit
```

Run `idf.py reconfigure` (or `idf.py build`). The manager clones into
`managed_components/`. Keep `REQUIRES esp_now_midi` (or the dependency key
name you chose) on the consuming component.

### 3. Local checkout via `EXTRA_COMPONENT_DIRS`

If the library lives outside your project tree, either:

**A.** Point `EXTRA_COMPONENT_DIRS` at a directory that *contains* the
component folder (not the component itself):

```cmake
# your-idf-project/CMakeLists.txt (before include project.cmake)
set(EXTRA_COMPONENT_DIRS "/path/to/parent-of-esp_now_midi")
```

…where that parent contains a subdirectory named `esp_now_midi` (clone or
symlink the repo there).

**B.** Use a thin wrapper component (same pattern as the repo smoke test in
[`test/idf`](https://github.com/grantler-instruments/ESP-NOW-MIDI/tree/main/test/idf)):

```
your-project/
  components/esp_now_midi/CMakeLists.txt   # INCLUDE_DIRS → absolute path to checkout
  main/
```

Example wrapper `CMakeLists.txt`:

```cmake
get_filename_component(ESP_NOW_MIDI_DIR "/path/to/ESP-NOW-MIDI" ABSOLUTE)

idf_component_register(
    INCLUDE_DIRS "${ESP_NOW_MIDI_DIR}"
    REQUIRES esp_wifi esp_netif nvs_flash
)
```

### Compile smoke test (this repo)

To verify the core builds against your installed IDF:

```bash
cd test/idf
. $IDF_PATH/export.sh
idf.py set-target esp32s3
idf.py build
```

See [`test/idf/README.md`](https://github.com/grantler-instruments/ESP-NOW-MIDI/blob/main/test/idf/README.md).

## Enomik examples (pure IDF)

| Example | Role |
|---------|------|
| [`examples_idf/client`](https://github.com/grantler-instruments/ESP-NOW-MIDI/tree/main/examples_idf/client) | `enomik::Client` — SysEx-configurable I/O |
| [`examples_idf/client_test`](https://github.com/grantler-instruments/ESP-NOW-MIDI/tree/main/examples_idf/client_test) | Deterministic DUT for [`scripts/wizard`](https://github.com/grantler-instruments/ESP-NOW-MIDI/tree/main/scripts/wizard) |
| [`examples_idf/dongle`](https://github.com/grantler-instruments/ESP-NOW-MIDI/tree/main/examples_idf/dongle) | `enomik::Dongle` — USB MIDI ↔ ESP-NOW (S2/S3); optional SSD1306 |

Dongle / USB MIDI clients need a native-USB target (`idf.py set-target esp32s2`
or `esp32s3`). Classic ESP32 can still run the core transport and
non-USB client I/O.

## Supporting headers

Public headers live under `include/` (that directory is the IDF include path):

| Header | Role |
|--------|------|
| [`esp_now_midi.h`](api/Classes/classesp__now__midi.md) | Core transport |
| [`esp_now_midi_helpers.h`](api/Files/esp__now__midi__helpers_8h/) | Packet types and MIDI helpers |
| `esp_now_midi_log.h` | `EspNowMidiLog` → `ESP_LOG*`; see [Logging](logging.md) |
| `esp_now_midi_wifi.h` | STA bring-up when `manageWifi` is true (see below) |
| `esp_now_midi_gpio.h` | Digital / PWM / ADC / touch (Arduino + IDF) |
| `esp_now_midi_prefs.h` | NVS-backed prefs subset |
| `esp_now_midi_usb.h` | USB MIDI via `esp_tinyusb` (S2/S3) |
| `esp_now_midi_compat.h` | `map` / `millis` / `constrain` fallbacks |
| `version.h` | Semver macros / `getVersion()` |
| `PeerStorage.h` | Peer list persistence (Arduino EEPROM / IDF NVS) |

## Wi-Fi

ESP-NOW needs Wi-Fi in station (STA) mode. `esp_now_midi_wifi.h` brings it up
when asked; you can also own Wi-Fi yourself.

### Backends

| Build | Backend |
|-------|---------|
| Arduino (including Arduino-ESP32) | `WiFi.mode(WIFI_STA)` / `WiFi.disconnect()` |
| Pure ESP-IDF (`ESP_PLATFORM` without `ARDUINO`) | `nvs_flash` + `esp_netif` + `esp_wifi` STA start |
| Host / native tests | No-op |

### `manageWifi`

`begin()` takes an optional fourth argument (default `true`):

```cpp
bool begin(bool reducePowerAtCostOfLatency = false,
           bool autoPeerDiscovery = true,
           DataSentCallback callback = DefaultOnDataSent,
           bool manageWifi = true);
```

| `manageWifi` | Behavior |
|--------------|----------|
| `true` (default) | Library brings up Wi-Fi STA, then starts ESP-NOW |
| `false` | App must already have started Wi-Fi; library only configures ESP-NOW, channel (`ESP_NOW_MIDI_CHANNEL`), and power |

```cpp
esp_now_midi transport;

// Library brings up STA (default):
transport.begin();

// Or you own Wi-Fi already:
transport.begin(false, true, esp_now_midi::DefaultOnDataSent, /*manageWifi*/ false);
```

Peers must share the **same radio channel**. With `manageWifi = false` and an
AP association, ESP-NOW follows the AP channel — keep `ESP_NOW_MIDI_CHANNEL`
aligned with that.

## Browse generated reference

The Doxygen pages cover the shared C++ headers, including Enomik types used by
both Arduino and IDF examples.

- [Classes](api/Classes/index.md)
- [Files](api/Files/index.md)
- [Namespaces](api/Namespaces/index.md) — includes `esp_now_midi_wifi`
