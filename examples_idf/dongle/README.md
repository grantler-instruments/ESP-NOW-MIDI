# enomik::Dongle - pure ESP-IDF example

USB MIDI <-> ESP-NOW bridge, built against bare ESP-IDF (no Arduino). Pure-IDF
port of [`examples/dongle`](../../examples/dongle) - same `enomik::Dongle`
API, no display support (the Arduino example's `SSD1306Display.h` is
Adafruit_GFX-only and hasn't been ported).

## Build

Requires a native-USB target (ESP32-S2 or ESP32-S3):

```bash
cd examples_idf/dongle
. $IDF_PATH/export.sh
idf.py set-target esp32s3
idf.py build
```

## Status: unverified

The USB MIDI backend (`include/esp_now_midi_usb.h`) was written against a raw
`tinyusb` reference implementation without a local IDF SDK to build-verify
against - component names (`esp_tinyusb` in `components/esp_now_midi_dongle/CMakeLists.txt`
and `idf_component.yml`), the `CONFIG_TINYUSB_ENABLED` sdkconfig option, and
the exact ESP32 USB PHY bring-up call may need correction against your IDF
version. If the device doesn't enumerate, compare against the official
`$IDF_PATH/examples/peripherals/usb/device/tusb_midi` example first.

## Layout

- `main/` — `app_main` that creates an `enomik::Dongle`, calls `begin()`, and
  polls `loop()`
- `components/esp_now_midi_dongle/` — CMake wrapper exposing repo root
  (for `enomik_dongle.h`, which is intentionally not part of the core
  `include/` surface) plus `include/`
