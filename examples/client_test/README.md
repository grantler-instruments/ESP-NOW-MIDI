# client_test

A deterministic device-under-test firmware for the test wizard
(`scripts/wizard`). It is **not** a product sketch — its behaviour is fixed so a
host script can drive and verify it step by step.

All interaction with the harness is over **MIDI only** (USB in Phase 1, ESP-NOW
via the dongle in Phase 2). There is no parallel serial protocol.

## Behaviour contract

- Echoes channel-voice messages back **only on channel 10** (USB + ESP-NOW):
  note on/off, control change, program change, pitch bend (center / ±1000),
  channel aftertouch, poly aftertouch. Other channels are not echoed.
- Periodically sends a registration handshake (CC 127 on channel 16) so the
  dongle can auto-discover this client once its MAC has been stored.
- Button pins **9**, **16**, and **17** (INPUT_PULLUP, active low → notes 62, 60,
  61 on ch 1) are configured at runtime over SysEx by the wizard.

## Requirements

- USB-capable board (ESP32-S2 / S3) with TinyUSB. On S3, set USB mode to
  USB-OTG (TinyUSB).
- Dependencies: Adafruit TinyUSB Library, MIDI Library (FortySevenEffects).
- Optional momentary buttons from GPIO 16 and 17 to GND for the button tests.

`HAS_USB_MIDI` is enabled in this example's `config.h`; that is what exposes the
USB MIDI port used in Phase 1.

See `scripts/wizard/README.md` for how to drive this firmware.
