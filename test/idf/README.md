# ESP-IDF compile smoke test

Minimal IDF project that links the `esp_now_midi` core (Wi-Fi + ESP-NOW MIDI)
without Arduino.

## Build

```bash
cd test/idf
. $IDF_PATH/export.sh   # or: . ~/.espressif/v6.0.1/esp-idf/export.sh
idf.py set-target esp32s3
idf.py build
```

Other targets (`esp32`, `esp32s2`, …) work the same way.

## Layout

- `main/` — `app_main` that calls `esp_now_midi::begin()` and `sendNoteOn`
- `components/esp_now_midi/` — CMake wrapper pointing at repo `include/`
  (core public headers only; same surface as the root IDF `CMakeLists.txt`)
