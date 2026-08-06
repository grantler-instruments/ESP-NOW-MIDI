# API reference

ESP-NOW MIDI is available for three targets. Pick the reference that matches
your build:

| Target | Entry | Notes |
|--------|-------|-------|
| [C++ Arduino](arduino-api.md) | Full library: `esp_now_midi`, `enomik::Client`, `enomik::Dongle`, SysEx I/O | Arduino-ESP32; Doxygen-generated class/file/namespace pages |
| [C++ ESP-IDF](idf-api.md) | Core transport + Enomik | Same headers as Arduino; Wi-Fi / logging / GPIO / USB backends switch via `#ifdef`. See `examples_idf/` for `enomik::Client` and `enomik::Dongle`. |
| [CircuitPython](circuitpython-api.md) | `esp_now_midi.py` | Same on-air packet format as the C++ core |

Shared guide: [Logging](logging.md). Wi-Fi / `manageWifi` for the C++ core is
documented under [C++ ESP-IDF](idf-api.md#wi-fi) (same `begin()` API on Arduino).
