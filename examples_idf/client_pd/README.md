# enomik::Client + Pure Data (libpd) — pure ESP-IDF example

An enomik client that runs a Pd patch on the ESP32. Pd's own MIDI objects are
connected to ESP-NOW MIDI (and to USB MIDI on ESP32-S2/S3) through
`enomik::Client`, and `adc~` / `dac~` run over I2S.

```
dongle / other clients ─ESP-NOW─┐                ┌─ [notein] [ctlin] [bendin] [pgmin]
USB MIDI (S2/S3) ───────────────┼─ enomik::Client ┤   [touchin] [polytouchin] [midiin]
                                │                │   [sysexin] [midirealtimein]
                                └────────────────┴─ [noteout] [ctlout] [bendout] [pgmout]
                                                     [touchout] [polytouchout] [midiout]
I2S ADC/mic ──> [adc~ 1 2]          [dac~ 1 2] ──> I2S DAC
```

Pd runs inside the firmware through [libpd](https://github.com/libpd/libpd)
(libpd 0.16.1 / Pd 0.56-5). CMake clones it into `components/libpd/libpd/` on the first build (needs git and network once); that folder is gitignored and kept across `idf.py fullclean`. To change the version, edit `LIBPD_TAG` in `components/libpd/CMakeLists.txt` and delete the folder. It does not use
[espd](https://github.com/ben-wes/espd).

## Hardware

ESP32-S3 with PSRAM (tested build target). Pd allocates most of its memory
from PSRAM. Classic ESP32 / S2 are not tested.

`sdkconfig.defaults.esp32s3` assumes **octal** PSRAM (N8R8, N16R8 modules).
For quad PSRAM (N8R2, N4R2), remove `CONFIG_SPIRAM_MODE_OCT=y` before the
first build.

Default I2S pins (change in `idf.py menuconfig` → **client_pd**):

| Signal | GPIO | e.g. PCM5102A / MAX98357A | e.g. INMP441 |
|--------|------|---------------------------|--------------|
| BCLK   | 5    | BCK / BCLK                | SCK          |
| WS     | 6    | LCK / LRC                 | WS           |
| DOUT   | 7    | DIN                       | –            |
| DIN    | 15   | –                         | SD           |
| MCLK   | –1   | (SCK to GND on PCM5102A)  | –            |

The format is I2S Philips, 32-bit slots, stereo. Set DIN or DOUT to `-1` to
leave that direction out.

## Build and flash

```bash
cd examples_idf/client_pd
. $IDF_PATH/export.sh            # ESP-IDF 6.0
idf.py set-target esp32s3
idf.py build
idf.py -p PORT flash monitor
```

`idf.py flash` writes the firmware and a LittleFS image built from `pd/`
(partition `pd`, 960 KB) together.

## Patches

The firmware opens `pd/main.pd` at boot. Everything in `pd/` is on the search
path, so abstractions and other files (`.pd`, `.txt`, `.wav`) next to
`main.pd` work. Edit them on your computer in Pd, then `idf.py flash` again.

`[print]` output goes to the serial monitor (tag `pd`).

The demo `main.pd` plays incoming notes on a sine with a short envelope and
sends the input level from `adc~` as CC 20 on channel 1.

### MIDI mapping

- Channels: Pd 1–16 = enomik 1–16. Port numbers above 16 (`[noteout 17]`)
  are folded onto channels 1–16.
- Note Off arrives as `[notein]` velocity 0. `[noteout]` with velocity 0 is
  sent as Note On velocity 0, which receivers treat as Note Off.
- `[midiout]` raw bytes are reassembled into messages: channel messages,
  song position, song select, realtime (clock, start, continue, stop) and
  SysEx (`F0 … F7`, up to 1024 bytes). MTC quarter frames, tune request,
  active sensing and reset are not forwarded.
- Clock, start, continue and stop arrive in `[midirealtimein]`; song position
  and song select in `[midiin]`.
- SysEx (ESP-NOW and USB) arrives in `[sysexin]`, framed `F0 … F7`. USB SysEx
  also still reaches the enomik config protocol, so the enomik app can
  configure the client's pins as usual.

## Configuration (`idf.py menuconfig` → client_pd)

| Option | Default | |
|--------|---------|---|
| Sample rate | 48000 | |
| Pd blocks per audio buffer | 2 | 2 × 64 = 128 frames, 2.7 ms at 48 kHz |
| CPU core for the Pd task | 1 | WiFi runs on core 0 |
| I2S audio | on | Off: control-only, Pd is ticked by a timer at block rate, `adc~` is silent and `dac~` goes nowhere |

## Layout

- `main/main.cpp` — mounts the `pd` LittleFS partition, starts the enomik
  client and the Pd task
- `main/pd_runtime.cpp` — the Pd task: libpd init, opens the patch, loops
  MIDI → `adc~` → Pd → `dac~`. `pd_runtime_request_reload()` reopens the
  patch (for updating patch files at runtime later)
- `main/pd_enomik_bridge.cpp` — enomik handlers → `libpd_noteon()` …;
  libpd MIDI hooks → `Client::sendNoteOn()` …
- `main/pd_audio_i2s.cpp` — full-duplex I2S, int32 ↔ float
- `components/libpd/` — libpd + Pd as an IDF component; `compat/` has the
  POSIX bits newlib lacks (stubs for GUI startup, `glob`, `lstat`, …)
- `components/esp_now_midi_client/` — exposes `enomik_client.h` and
  `include/` from the repo root

## Limitations

- No externals (no dynamic loading). Built in: Pd vanilla plus `bob~`,
  `bonk~`, `choice`, `fiddle~`, `loop~`, `lrshift~`, `pique`, `sigmund~`.
  Not built: `pd~`, `stdout`.
- No GUI, one patch (`main.pd`).
- `[file glob]` finds nothing; `[netsend]` / `[netreceive]` compile, but
  the client does not join a WiFi network (ESP-NOW only).
- Allocations above 4 KB (tables, delay lines, large arrays) go to PSRAM,
  which is slower than internal RAM. Heavy patches may need more blocks per
  buffer.
