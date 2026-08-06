# client_test — pure ESP-IDF example

ESP-IDF port of [`examples/client_test`](../../examples/client_test): deterministic
device-under-test for [`scripts/wizard`](../../scripts/wizard). Same MIDI-only
contract (echo on channel 10, periodic handshake, SysEx-configured buttons).

## Build

Requires a native-USB target (ESP32-S2 or ESP32-S3):

```bash
cd examples_idf/client_test
. $IDF_PATH/export.sh
idf.py set-target esp32s2
idf.py build
idf.py -p PORT flash monitor
```

## Behaviour contract

Same as the Arduino sketch — see [`examples/client_test/README.md`](../../examples/client_test/README.md).

## Layout

- `main/` — `app_main` with echo handlers + handshake loop
- `main/config.h` — `TEST_*` constants (same as Arduino)
- `components/esp_now_midi_client_test/` — CMake wrapper for `enomik_client.h` + `include/`
