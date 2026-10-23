# ESP-NOW MIDI board tester and client_test wizard

Two scripts, both started with `run.sh`:

- `./run.sh`: the **board tester** for boards running the `client` sketch
- `./run.sh client_test`: the **client_test wizard**, a regression run of the
  library against the `client_test` firmware (see below)

## Board tester (`wizard.py`)

Terminal version of the [enomik app](https://grantler-instruments.github.io/enomik-app/)'s
tester page (`#/tester`), for boards running the `client` sketch. With the
client and a dongle both on USB, it sets up the pins, then checks step by step
that every button press arrives over the client's USB **and** wirelessly at the
dongle, that the LED lights through MIDI loopback, and that the touch pads
send. All checks use MIDI only (enomik SysEx and channel voice messages).

### Steps

1. **Connect client and dongle**: finds both MIDI ports, reads each board's
   firmware version and MAC (`GET_VERSION`, `GET_MAC`), then sets up the
   client:
   - adds the dongle as peer (its MAC comes from the dongle, no typing)
   - pin 16: input pull-up → note on 16 (changes type, see step 2)
   - pin 17: input pull-up → CC 17
   - LED pin (default 15): output ← note 15
   - touch pins (default 1, 2): touch, 20 % threshold → CC *pin*

   On a failure: retry, continue anyway, or quit.
2. **Press the buttons**: pin 16 sends note on, note off, control change and
   pitch bend in turn, switching to the next type after each release; pin 17
   sends CC. Each row needs a press over USB and wirelessly.
3. **LED loopback**: MIDI loopback is switched on (only during this step; it is
   saved on the device) and pin 16 sends note *LED pin*. Hold the button: the
   LED should light. The note must arrive over USB and wirelessly, then answer
   **y**/**n** whether the LED lit. **Esc** skips.
4. **Touch pins**: shows the live reading of each pad (raw stream, firmware
   0.19 and later) and whether its CC arrived over USB and wirelessly.

A step whose test passed moves on by itself; **Enter** moves on (skips) early.
Then the results, and the offer to write the **default configuration** the
board ships with: `RESET`, pins 16 and 17 send CC 16 and 17, the LED follows
CC 16 through MIDI loopback (loopback on), and the tested dongle stays a peer.

The tester exits with a non-zero status if any test failed.

### Hardware

- Board under test: the `client` example (USB MIDI, ESP32-S2/S3).
  Buttons from pins **16** and **17** to GND. The onboard LED (pin 15 on the
  Lolin S2 mini) and the touch pins are on the board.
- Dongle: the `dongle` example, firmware 0.19 or later (older dongles don't
  report their MAC, so wireless presses won't arrive).

Without a dongle (`--no-dongle`, or none found) only USB is checked.

### Run (isolated — no system Python changes)

Dependencies (`mido`, `python-rtmidi`) are installed only into
`scripts/wizard/.venv/`, which is gitignored. Your system Python is not modified.

```bash
cd scripts/wizard
./run.sh
./run.sh --led-pin 21 --touch-pins 3,4
./run.sh --no-dongle
```

The first run creates `.venv/` and installs requirements there automatically.

Flags:
- `--client-port SUBSTR` / `--dongle-port SUBSTR` to force a MIDI port by name
  when auto-detection picks the wrong one.
  Auto-detection matches `client` or `dongle`; a generic `enomik` name requires
  manual selection so the opposite device cannot be selected automatically.
- `--no-dongle` — USB only
- `--led-pin N` — onboard LED (default 15)
- `--touch-pins "1, 2"` — touch pads, comma separated (`""` for none)

## client_test wizard (`client_test.py`)

Regression harness for the `examples/client_test` firmware, run before a
release. It checks each step automatically and reports PASS / FAIL. All checks
use **MIDI only** (channel voice, system messages and SysEx); there is no
serial side-channel.

Inputs are tested twice: first with **fake values** (the wizard tells
`client_test` over channel 15 what each input pin should read), then with real
buttons and a potentiometer. Press **Esc** to skip a physical step, or use
`--io virtual` to run only the fake inputs.

### What it tests

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
   Raw stream: `SET_RAW_STREAM` on analog pin 10 sends `RAW_VALUE` frames
   every 50 ms with the fake level, a threshold change shows up in the frames,
   frames stop after `SET_RAW_STREAM` off, unconfigured pin → `PIN_NOT_FOUND`
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

First waits until an echo comes back over the dongle (up to 15 s) and sets the
fake inputs: after a reboot the client reads its real pins, and unconnected
analog pins read noise and flood MIDI.

Steps 4, 5, 6, 8, 9 and 10 again over the air. The burst allows up to 5% loss.
The 300-byte SysEx is split into two ESP-NOW frames and reassembled by the dongle.

> The dongle does not bridge configuration SysEx, so SysEx steps run in Phase 1 only.

### Run

```bash
cd scripts/wizard
./run.sh client_test              # both phases
./run.sh client_test --phase 1    # USB only
./run.sh client_test --phase 2    # ESP-NOW only
```

Hardware:
- Flash `examples/client_test` onto a USB-capable board (ESP32-S2/S3, TinyUSB).
- For the real-input steps (skip them with Esc or `--io virtual`):
  - Pins **16** and **17**: momentary buttons to GND (INPUT_PULLUP).
  - Pin **10**: potentiometer (wiper to GPIO, ends to 3V3 / GND).
  - Pin **21**: optional PWM / LED load (driven by CC 17 via MIDI loopback).
- For Phase 2, flash the `dongle` example onto a second board. With the dongle
  plugged in at the start, the wizard reads its MAC (`GET_MAC`, dongle firmware
  0.19+); otherwise it asks for it (`print_mac`, dongle display or serial log).

Flags:
- `--dongle-mac XX:XX:XX:XX:XX:XX` — dongle MAC (default: read from the dongle, else asked)
- `--client-port SUBSTR` / `--dongle-port SUBSTR` to force a MIDI port by name
  when auto-detection picks the wrong one.
- `--io virtual|physical|both` — fake inputs, real buttons/pot, or both (default)
- `--burst N` — notes in the burst test, 1..128 (`0` skips it, default 100)

Example:

```bash
./run.sh client_test --dongle-mac 84:F7:03:F2:54:62
./run.sh client_test --phase 1 --io virtual
```

It exits with a non-zero status if any step fails.

### Notes
- Channels are 1-based here to match the library (`mido` uses 0-based on the
  wire; the wizard converts internally).
- Each real button / pot test waits for **Enter** before it listens, so a
  floating pin cannot pass it early; **Esc** skips it. During a step, MIDI
  advances it automatically; **Esc** still skips.
- Test cases live in `cases.py` (no MIDI dependency); `test/protocol` unit-tests
  them.
- The firmware sends a SysEx ANNOUNCE every 2 s while idle so the dongle
  discovers it; the dongle keeps announces to itself, so the wizard never sees them.

## Files

- `tester.py`: pin configs, reply parsing, test state (no MIDI dependency;
  `test/protocol/test_wizard_tester.py` tests it)
- `wizard.py`: the board tester's interactive steps, with `mido`
- `client_test.py`: the client_test wizard
- `cases.py`: client_test wizard test cases (no MIDI dependency;
  `test/protocol/test_wizard_cases.py` tests them)
- `enomik_sysex.py`: enomik SysEx builders and parser
- `midi_io.py`: port detection and key input
