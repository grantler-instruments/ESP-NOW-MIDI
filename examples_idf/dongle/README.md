# enomik::Dongle — pure ESP-IDF example

USB MIDI ↔ ESP-NOW bridge, built against bare ESP-IDF (no Arduino). Pure-IDF
port of [`examples/dongle`](../../examples/dongle) — same `enomik::Dongle`
API, including an optional SSD1306 status display (`main/ssd1306_display.h`,
no Adafruit_GFX).

## Build

Requires a native-USB target (ESP32-S2 or ESP32-S3):

```bash
cd examples_idf/dongle
. $IDF_PATH/export.sh
idf.py set-target esp32s2   # or esp32s3
idf.py build
idf.py -p PORT flash monitor
```

USB MIDI uses `espressif/esp_tinyusb` (`include/esp_now_midi_usb.h`), verified
against ESP-IDF v6.0.1. Enable `CONFIG_TINYUSB_MIDI_COUNT=1` via
`sdkconfig.defaults` (already set here). If the device does not enumerate,
compare against `$IDF_PATH/examples/peripherals/usb/device/tusb_midi`.

## Display

`main.cpp` registers `SSD1306Display` with `setDisplay()` before `begin()`.
Defaults match a Lolin S2 Mini OLED (SDA=33, SCL=35). Override with
`-DOLED_SDA_GPIO=…` / `-DOLED_SCL_GPIO=…` if your board differs.

## Layout

- `main/` — `app_main` that creates an `enomik::Dongle`, calls `begin()`, and
  polls `loop()`; optional SSD1306 UI
- `components/esp_now_midi_dongle/` — CMake wrapper exposing repo root
  (for `enomik_dongle.h`, which is intentionally not part of the core
  `include/` surface) plus `include/`
