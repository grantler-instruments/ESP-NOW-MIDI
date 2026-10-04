# Changelog

## Unreleased

- Dongle: USB watchdog (default on). When the computer stops taking MIDI from the dongle while messages are waiting, the dongle re-attaches USB like a cable replug: when USB is suspended and the computer refuses remote wakeup (at most once per suspend), or when the MIDI IN endpoint is not read for 500 ms although the computer read from the dongle before and is not sending MIDI itself. Backs off 30 s doubling to 10 min if a re-attach did not help. Remote wakeup is now sent at most once per second instead of on every loop. `setUsbWatchdogMode(Off|Observe|Recover)`, `usbWatchdogConfig()`, `getUsbHealthStats()`, `getUsbHealthStatsPrevious()` (saved to flash only after a USB incident). Grantler dongle: new read-only USB menu page; `ENOMIK_USB_FAULT_INJECT` test flag.
- Dongle: drop stale USB MIDI messages when the host stops reading (`USB_MIDI_STALE_MS`, default 500 ms, `setUsbStaleTimeout()`); release messages and the latest CC/pitch bend/pressure/program values are always delivered. A full queue now evicts the oldest droppable message instead of the newest. New drop counters `getUsbStaleDropCount()` / `getUsbOverflowDropCount()`.

## 0.18.0

- PlatformIO support (`library.json`, per-example `platformio.ini`, CI example builds)

## 0.17.0

- Initial ESP-IDF support (core transport, Enomik Client/Dongle, `examples_idf/`)
