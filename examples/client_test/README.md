# client_test

A deterministic device-under-test firmware for the test wizard
(`scripts/wizard`). It is **not** a product sketch — its behaviour is fixed so a
host script can drive and verify it step by step.

All interaction with the harness is over **MIDI only** (USB in Phase 1, ESP-NOW
via the dongle in Phase 2). There is no parallel serial protocol.

## Behaviour contract

- Echoes channel-voice messages back **only on channel 10** (USB + ESP-NOW):
  note on/off, control change, program change, pitch bend, channel aftertouch,
  poly aftertouch. Other channels are not echoed.
- Echoes system messages back on any channel: clock, start, continue, stop,
  song position, song select.
- Fake inputs, driven by the wizard on **channel 15**, so no wiring is needed:
  - CC *n* = *v*: analog pin *n* reads level *v* (0–127)
  - note on *n*: digital pin *n* reads LOW (pressed); note off *n*: HIGH
  - program change: all pins read the hardware again
- API sends, triggered by the wizard: program change *p* on **channel 14** makes
  the board call `enomik::Client` send function number *p* (`runApiCase()`);
  channel messages go out on **channel 12**. Keep `runApiCase()` in sync with
  `API_CASES` in `scripts/wizard/cases.py` (a unit test checks this).
- Sends the library's SysEx ANNOUNCE every 2 s while idle (`setHeartbeatInterval`) so the
  dongle can auto-discover this client once its MAC has been stored.
- I/O pins are configured at runtime over SysEx by the wizard:
  - **16** / **17** INPUT_PULLUP → CC 16 / 17
  - **10** analog input → CC 10
  - **21** analog output ← CC 17 (with MIDI loopback enabled)
  - **18** INPUT_PULLUP → note 60
  - **11** analog input → pitch bend

## Requirements

- USB-capable board (ESP32-S2 / S3) with TinyUSB. On S3, set USB mode to
  USB-OTG (TinyUSB).
- Dependencies: Adafruit TinyUSB Library, MIDI Library (FortySevenEffects).
- Optional, for the real-input steps: buttons on GPIO 16/17 to GND, pot on
  GPIO 10, PWM/LED on GPIO 21.

`HAS_USB_MIDI` is enabled in this example's `config.h`; that is what exposes the
USB MIDI port used in Phase 1.

Drive it with `scripts/wizard/run.sh client_test`; see `scripts/wizard/README.md`.
