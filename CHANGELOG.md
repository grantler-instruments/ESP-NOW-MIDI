# Changelog

## 0.19.0

- SysEx over ESP-NOW: versioned fragment protocol (max 1024 bytes, 240-byte payloads), receive callback, Client/Dongle bridging
- Breaking: replaces the unused fixed 129-byte SysEx blob; CircuitPython `send_sysex` updated to match
- Enomik config SysEx replies stay USB-only (no longer blasted over ESP-NOW)
- `midi_sysex_message` capacity expanded to 256 bytes for USB/config path

## 0.18.0

- PlatformIO support (`library.json`, per-example `platformio.ini`, CI example builds)

## 0.17.0

- Initial ESP-IDF support (core transport, Enomik Client/Dongle, `examples_idf/`)
