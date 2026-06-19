# ESP-NOW MIDI test wizard

Interactive, step-by-step harness for the `client_test` firmware. It walks you
through bringing up a client board, prompts you when manual action is needed
(button presses), and automatically checks each step. All checks use **MIDI only**
(channel voice + SysEx); there is no serial side-channel.

## What it tests

**Phase 1 — client over USB MIDI**
1. SysEx `GET_MAC` round-trip
2. `SET_PIN_CONFIG` for pins 9, 16, and 17 (INPUT_PULLUP → notes 62, 60, 61)
3. Echo suite on **channel 10**: note on/off, CC, program change, pitch bend
   (center, up, down), channel aftertouch, poly aftertouch; plus verify channel 1
   is **not** echoed
4. `ADD_PEER` with the dongle MAC (stored on the client for Phase 2)
5. Press button on pin 16 → note 60
6. Press button on pin 17 → note 61
7. Press button on pin 9 → note 62

**Phase 2 — client over ESP-NOW (through the dongle)**
1. Same echo suite on channel 10 (wireless)
2. Press button on pin 16 → note 60
3. Press button on pin 17 → note 61
4. Press button on pin 9 → note 62

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
- Pins **9**, **16**, and **17**: momentary buttons to GND (INPUT_PULLUP).
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
- Button steps advance automatically when MIDI arrives — no Enter.
  Press **Esc** during those steps to skip the current test.
- A registration handshake (CC 127 on channel 16) is emitted periodically by the
  firmware; the wizard ignores it.
