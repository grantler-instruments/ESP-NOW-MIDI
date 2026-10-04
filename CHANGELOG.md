# Changelog

## Unreleased

- Client: MIDI received over ESP-NOW is now handled in `loop()` instead of the ESP-NOW receive callback (WiFi task), so handlers and pin outputs no longer race with `loop()` (e.g. a SysEx pin configuration arriving while notes are received). **Behavior change:** handlers react as often as `loop()` is called; avoid long `delay()`s. `setDispatchInLoop(false)` restores the old immediate dispatch in the WiFi task.
- Dongle: ESP-NOW receive (mute check, to-host filter, message history) is handled in `loop()`; the receive callback only queues.
- Core: `setHandleMessage()` delivers every received message with its sender MAC, before the per-type handlers.
- Hello MIDI example: sends on a `millis()` timer instead of `delay()`, so received MIDI is handled right away; fixed its inverted send-error check.
- Core: `esp_now_midi.h` can be included from several source files (no duplicate `_instance` definition).
- Fix: the ESP-NOW peer list is now safe to use from the loop task and the receive callback at the same time (auto-discovery, echo handlers). Before, concurrent access could corrupt the list or leave peers registered in ESP-NOW but missing from the list, so they stopped receiving until a reboot. New `getPeer(index, mac)` and `copyPeers()` return copies.
- Fix: the dongle sent MIDI over ESP-NOW as its internal 7-byte struct (since the peer mute feature), which every receiver ignored as SysEx. It sends the standard 1-3 byte MIDI packet again.
- Fix: receiving a 4-128 byte ESP-NOW packet read past the end of the receive buffer (copied 129 bytes regardless of length).
- Dongle: USB watchdog (default on). When the computer stops taking MIDI from the dongle while messages are waiting, the dongle re-attaches USB like a cable replug: when USB is suspended and the computer refuses remote wakeup (at most once per suspend), or when the MIDI IN endpoint is not read for 500 ms although the computer read from the dongle before and is not sending MIDI itself. Backs off 30 s doubling to 10 min if a re-attach did not help. Remote wakeup is now sent at most once per second instead of on every loop. `setUsbWatchdogMode(Off|Observe|Recover)`, `usbWatchdogConfig()`, `getUsbHealthStats()`, `getUsbHealthStatsPrevious()` (saved to flash only after a USB incident). Grantler dongle: new read-only USB menu page; `ENOMIK_USB_FAULT_INJECT` test flag.
- Dongle: drop stale USB MIDI messages when the host stops reading (`USB_MIDI_STALE_MS`, default 500 ms, `setUsbStaleTimeout()`); release messages and the latest CC/pitch bend/pressure/program values are always delivered. A full queue now evicts the oldest droppable message instead of the newest. New drop counters `getUsbStaleDropCount()` / `getUsbOverflowDropCount()`.

## 0.18.0

- PlatformIO support (`library.json`, per-example `platformio.ini`, CI example builds)

## 0.17.0

- Initial ESP-IDF support (core transport, Enomik Client/Dongle, `examples_idf/`)
