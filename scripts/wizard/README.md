# ESP-NOW MIDI test wizard

Interactive, step-by-step harness for the `client_test` firmware. It walks you
through bringing up a client board, prompts you when manual action is needed
(button presses / pot turns), and automatically checks each step. All checks use
**MIDI only** (channel voice + SysEx); there is no serial side-channel.

## What it tests

**Phase 1 — client over USB MIDI**
1. SysEx `GET_MAC` round-trip
2. `SET_MIDI_LOOPBACK` on
3. `SET_PIN_CONFIG`:
   - pin 16 digital INPUT_PULLUP → CC 16
   - pin 17 digital INPUT_PULLUP → CC 17
   - pin 10 analog input → CC 10
   - pin 21 analog output ← CC 17 (driven via loopback when pin 17 fires)
4. Echo suite on **channel 10**: note on/off, CC, program change, pitch bend
   (center, up, down), channel aftertouch, poly aftertouch; plus verify channel 1
   is **not** echoed
5. `ADD_PEER` with the dongle MAC (stored on the client for Phase 2)
6. Press button on pin 16 → CC 16
7. Press button on pin 17 → CC 17
8. Turn potentiometer on pin 10 to CC 10 = 0, then 64, then 127 (prints each
   received value)

**Phase 2 — client over ESP-NOW (through the dongle)**
1. Same echo suite on channel 10 (wireless)
2. Press button on pin 16 → CC 16
3. Press button on pin 17 → CC 17
4. Same pot checks on pin 10 → CC 10 = 0, 64, 127

> The dongle bridges channel-voice MIDI only, so SysEx steps run in Phase 1 only.

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
- Pins **16** and **17**: momentary buttons to GND (INPUT_PULLUP).
- Pin **10**: potentiometer (wiper to GPIO, ends to 3V3 / GND).
- Pin **21**: optional PWM / LED load (driven by CC 17 via MIDI loopback).
- For Phase 2, flash the `dongle` example onto a second board and note its MAC
  (run `print_mac`, or read the dongle display / serial log).

Useful flags (pass through to `wizard.py`):
- `--dongle-mac XX:XX:XX:XX:XX:XX` — dongle MAC (skips the interactive prompt)
- `--client-port SUBSTR` / `--dongle-port SUBSTR` to force a MIDI port by name
  when auto-detection picks the wrong one.

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
- Button / pot steps advance automatically when MIDI arrives — no Enter.
  Press **Esc** during those steps to skip the current test.
- A registration handshake (CC 127 on channel 16) is emitted periodically by the
  firmware; the wizard ignores it.
