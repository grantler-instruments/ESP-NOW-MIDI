# ESP-NOW MIDI test wizard

Step-by-step harness for the `client_test` firmware. It checks each step
automatically and reports PASS / FAIL. All checks use **MIDI only** (channel
voice, system messages and SysEx); there is no serial side-channel.

Inputs are tested twice: first with **fake values** (the wizard tells
`client_test` over channel 15 what each input pin should read), then with real
buttons and a potentiometer. Press **Esc** to skip a physical step, or use
`--io virtual` to run only the fake inputs.

## What it tests

**Phase 1 — client over USB MIDI**
1. SysEx: `RESET`, `GET_VERSION` (matches the wizard), `GET_MAC`,
   `SET/GET_MIDI_LOOPBACK`, `SET/GET_POWER_SAVE`, `SET/GET_NAME` and clearing the
   name
2. `SET_PIN_CONFIG` for every input type, then `GET_PIN_CONFIG` and
   `GET_ALL_PIN_CONFIGS`:
   - pin 16 / 17 digital INPUT_PULLUP → CC 16 / 17
   - pin 10 analog input → CC 10
   - pin 21 analog output ← CC 17 (driven via loopback when pin 17 fires)
   - pin 18 digital INPUT_PULLUP → note 60
   - pin 11 analog input → pitch bend
3. SysEx error replies: unknown command, wrong protocol version, unconfigured
   pin, malformed MAC
4. Echo on **channel 10** of every channel voice message, including edge values
   (velocity 1/127, CC 0/127, program 0/127, pitch bend min/max/center,
   aftertouch 0/127); channel 1 is **not** echoed
5. Echo of system messages: clock, start, continue, stop, song position
   (0, 1000, 16383), song select
6. Burst: 100 notes back to back; all must come back. Prints round-trip times.
7. `ADD_PEER` with the dongle MAC (stored on the client for Phase 2), duplicate
   `ADD_PEER` error, `GET_PEER`, `GET_ALL_PEERS`, `GET_CONFIG` stream
8. API sends: the client calls each `enomik::Client` send function (note
   on/off, CC, program change, pitch bend min/center/max, aftertouch, poly
   aftertouch, clock, start, continue, stop, song position, song select, and
   SysEx of 8, 22 and 300 bytes) and the wizard checks what arrives on channel 12
9. Fake inputs: press/release pins 16, 17 and 18, analog pin 10 through CC 64,
   0, 127, 32, 96, analog pin 11 through pitch bend max, center and min
10. Real inputs: press the buttons on pins 16 and 17, turn the potentiometer on
    pin 10 to 0, 64 and 127

**Phase 2 — client over ESP-NOW (through the dongle)**

Steps 4, 5, 6, 8, 9 and 10 again over the air. The burst allows up to 5% loss.
The 300-byte SysEx is split into two ESP-NOW frames and reassembled by the dongle.

> The dongle does not bridge configuration SysEx, so SysEx steps run in Phase 1 only.

## Run (isolated — no system Python changes)

Dependencies (`mido`, `python-rtmidi`) are installed only into
`scripts/wizard/.venv/`, which is gitignored. Your system Python is not modified.

```bash
cd scripts/wizard
chmod +x run.sh   # once
./run.sh              # both phases
./run.sh --phase 1    # USB only
./run.sh --phase 2    # ESP-NOW only
```

The first run creates `.venv/` and installs requirements there automatically.

Hardware:
- Flash `examples/client_test` onto a USB-capable board (ESP32-S2/S3, TinyUSB).
- For the real-input steps (skip them with Esc or `--io virtual`):
  - Pins **16** and **17**: momentary buttons to GND (INPUT_PULLUP).
  - Pin **10**: potentiometer (wiper to GPIO, ends to 3V3 / GND).
  - Pin **21**: optional PWM / LED load (driven by CC 17 via MIDI loopback).
- For Phase 2, flash the `dongle` example onto a second board and note its MAC
  (run `print_mac`, or read the dongle display / serial log).

Useful flags (pass through to `wizard.py`):
- `--dongle-mac XX:XX:XX:XX:XX:XX` — dongle MAC (skips the interactive prompt)
- `--client-port SUBSTR` / `--dongle-port SUBSTR` to force a MIDI port by name
  when auto-detection picks the wrong one.
- `--io virtual|physical|both` — fake inputs, real buttons/pot, or both (default)
- `--burst N` — notes in the burst test, 1..128 (`0` skips it, default 100)

Example:

```bash
./run.sh --dongle-mac 84:F7:03:F2:54:62
./run.sh --phase 1
./run.sh --phase 2 --dongle-mac 84:F7:03:F2:54:62
```

The wizard exits with a non-zero status if any step fails.

## Notes
- Channels are 1-based here to match the library (`mido` uses 0-based on the
  wire; the wizard converts internally).
- Button / pot steps advance automatically when MIDI arrives — no Enter. Press **Esc** during those steps to skip the current test.
- Test cases live in `cases.py` (no MIDI dependency); `test/protocol` unit-tests
  them.
- The firmware sends a SysEx ANNOUNCE every 2 s while idle so the dongle
  discovers it; the dongle keeps announces to itself, so the wizard never sees them.
