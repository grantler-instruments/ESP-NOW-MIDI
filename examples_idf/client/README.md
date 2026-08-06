# enomik::Client — pure ESP-IDF example

Thin ESP-IDF port of [`examples/client`](../../examples/client): `enomik::Client`
with `begin()` / `loop()`, optional peer MAC, SysEx-configurable I/O over the
wire.

## Build

```bash
cd examples_idf/client
. $IDF_PATH/export.sh
idf.py set-target esp32s2   # or esp32 / esp32s3
idf.py build
idf.py -p PORT flash monitor
```

On ESP32-S2/S3, USB MIDI is enabled automatically (`HAS_USB_MIDI`). On classic
ESP32, the client is ESP-NOW + local I/O only.

## Layout

- `main/` — `app_main` that creates an `enomik::Client`, calls `begin()`, and
  polls `loop()`
- `components/esp_now_midi_client/` — CMake wrapper exposing repo root
  (for `enomik_client.h`) plus `include/`
