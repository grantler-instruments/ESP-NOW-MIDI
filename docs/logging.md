# Logging

Library internals use the static logger in `esp_now_midi_log.h`
(`EspNowMidiLog`) instead of calling `Serial` directly. Example sketches may
still use `Serial` for their own demo output.

## Levels

| Method | Meaning | Default |
|--------|---------|---------|
| `EspNowMidiLog::e` | Error | Always emitted (on supported backends) |
| `EspNowMidiLog::w` | Warning | Always emitted |
| `EspNowMidiLog::i` | Info | Always emitted |
| `EspNowMidiLog::d` | Debug | Only when `ESP_NOW_DEBUGGING` is `1` |

Helpers:

- `EspNowMidiLog::mac(prefix, mac)` — log a MAC at info level
- `EspNowMidiLog::formatMac(buf, len, mac)` — write `AA:BB:CC:DD:EE:FF` into a buffer

There is **no runtime `setLevel` API** today. Filtering is either compile-time
(`ESP_NOW_DEBUGGING` for debug) or, on ESP-IDF, the platform log level for the
library tag.

## Enable debug logs

Define the switch **before** including library headers (default is `0`):

```cpp
#define ESP_NOW_DEBUGGING 1
#include <esp_now_midi.h>
// or other library headers that pull in esp_now_midi_log.h
```

When `ESP_NOW_DEBUGGING` is `0`, calls to `EspNowMidiLog::d(...)` compile out.

## Backends

| Build | Backend |
|-------|---------|
| Arduino (including Arduino-ESP32) | `Serial.printf` with `[E|W|I|D][esp_now_midi] ...` |
| Pure ESP-IDF (`ESP_PLATFORM` without `ARDUINO`) | `ESP_LOGE` / `ESP_LOGW` / `ESP_LOGI` / `ESP_LOGD`, tag `esp_now_midi` |
| Host / native unit tests | No-op |

On ESP-IDF, use the usual log controls for that tag, for example:

```cpp
esp_log_level_set("esp_now_midi", ESP_LOG_DEBUG);
```

(or the equivalent `menuconfig` settings). That still does not enable
`EspNowMidiLog::d` unless `ESP_NOW_DEBUGGING` is `1` at compile time.
