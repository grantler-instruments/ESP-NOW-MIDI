# enomik_pd — Pure Data as an enomik client (ESP-IDF)

Runs a Pd patch on an ESP32 through [libpd](https://github.com/libpd/libpd)
(0.16.1, Pd 0.56-5). Pd's own MIDI objects are connected to ESP-NOW MIDI, and
to USB MIDI on ESP32-S2/S3, through `enomik::Client`. Audio goes through an
`AudioBackend`.

```
dongle / other clients ─ESP-NOW─┐                ┌─ [notein] [ctlin] [bendin] [pgmin]
USB MIDI (S2/S3) ───────────────┼─ enomik::Client ┤   [touchin] [polytouchin] [midiin]
                                │                │   [sysexin] [midirealtimein]
                                └────────────────┴─ [noteout] [ctlout] [bendout] [pgmout]
                                                     [touchout] [polytouchout] [midiout]
AudioBackend ──> [adc~]                [dac~] ──> AudioBackend
```

Examples: [`examples_idf/client_pd`](../../examples_idf/client_pd) (generic
I2S) and
[`examples_idf/client_pd_waveshare_audio_s3`](../../examples_idf/client_pd_waveshare_audio_s3).

## Components

This folder holds two IDF components:

- `enomik_pd/` — runtime, MIDI bridge, audio backends (this README)
- `libpd/` — libpd + Pd built for ESP-IDF. CMake clones libpd into
  `libpd/libpd/` on the first configure (needs git and network once). The
  folder is gitignored and kept across `idf.py fullclean`. To change version,
  edit `LIBPD_TAG` in `libpd/CMakeLists.txt` and delete the folder.
  `libpd/compat/` has the POSIX bits newlib lacks (stubs for GUI startup,
  `glob`, `lstat`, …).

## Use in a project

Needs the whole ESP-NOW-MIDI checkout (clone, submodule or local path):
`enomik_pd` includes `enomik_client.h` from the repository root.

Project `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.16)
list(APPEND EXTRA_COMPONENT_DIRS "path/to/ESP-NOW-MIDI/components")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(my_pd_client)
```

`main/CMakeLists.txt`:

```cmake
idf_component_register(SRCS "main.cpp" REQUIRES enomik_pd)
littlefs_create_partition_image(pd ../pd FLASH_IN_PROJECT) # optional: flash pd/ with the app
```

`main/main.cpp`:

```cpp
#include "enomik_pd.h"
#include "enomik_pd_i2s.h"

static enomik::pd::I2sStdBackend audio({.bclk = 5, .ws = 6, .dout = 7, .din = 15});

extern "C" void app_main(void)
{
    enomik::pd::mountLittleFs("pd", "/pd");

    enomik::pd::Config cfg;
    cfg.clientName = "my_pd_client";
    cfg.audio = &audio; // nullptr: control-only
    enomik::pd::begin(cfg);
}
```

Add a `pd` partition (`data, littlefs`) to your partition table. ESP-IDF ≥ 5.5;
built and tested with 6.0.1. ESP32-S3 with PSRAM recommended
(`CONFIG_SPIRAM_USE_MALLOC=y`); classic ESP32 builds too.

## API (`enomik_pd.h`)

| | |
|---|---|
| `bool begin(const Config &)` | Starts the enomik client and the Pd task (opens the patch, DSP on). Once. |
| `void requestReload()` | Closes and reopens the patch at the next buffer, from any task. For updating patch files at runtime. |
| `bool mountLittleFs(label, path)` | Mounts a LittleFS patch partition. Before `begin()`. |
| `enomik::Client &client()` | The client: add peers, stats. Its `send*` / `setHandle*` belong to the Pd task. |

`Config`:

| Field | Default | |
|-------|---------|---|
| `patchDir`, `patchFile` | `/pd`, `main.pd` | `patchDir` is on Pd's search path (abstractions, `.txt`, `.wav`) |
| `clientName` | `nullptr` | enomik name; `nullptr` keeps the default |
| `sampleRate` | 48000 | |
| `ticks` | 2 | Pd blocks (64 samples) per buffer: 2 = 128 frames, 2.7 ms at 48 kHz |
| `core`, `priority`, `stackSize` | 1, 22, 16384 | Pd task; WiFi runs on core 0, priority 23 |
| `audio` | `nullptr` | `AudioBackend *`; `nullptr` or a failed `begin()`: control-only, Pd ticked by a timer at block rate |

`[print]` output goes to the log (tag `pd`).

### Threading

libpd is not thread-safe. Everything Pd runs on one task created by
`begin()`: libpd, the enomik MIDI handlers (dispatched from
`Client::loop()`, which the Pd task calls once per buffer) and the audio
backend. A blocking `AudioBackend::write()` (or `read()`) paces the loop to
the sample clock.

## Audio backends

| Header | Class | Hardware |
|--------|-------|----------|
| `enomik_pd_i2s.h` | `I2sStdBackend(I2sPins, port)` | Plain I2S parts, no register setup: PCM5102A / MAX98357A out, INMP441 in. Philips, 32-bit slots, stereo, full duplex, ESP32 master. `dout` / `din` = -1 leaves that direction out. |
| `enomik_pd_waveshare_s3_audio.h` | `WaveshareS3AudioBackend(volume, micGainDb)` | Waveshare ESP32-S3-AUDIO-Board: ES8311 speaker, ES7210 two mics, TCA9555 amp enable. Uses `espressif/esp_codec_dev` (chip drivers, no BSP). ESP32-S3 only. |

Own backend: derive from `enomik::pd::AudioBackend` and implement `begin()`,
`inputChannels()`, `outputChannels()`, `read()`, `write()`. Samples are
interleaved floats, -1..1; `write()` must block until the hardware has room.

## MIDI mapping

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
- Received MIDI is handed to Pd once per buffer, so its timing is quantised
  to the buffer length (2.7 ms at the defaults).

## Patch upload (SysEx)

Patch files can be sent to the client over MIDI, through its own USB MIDI
port or through an enomik dongle (the dongle forwards non-enomik SysEx over
ESP-NOW to its clients and their answers back to the host). The protocol is
the standard **MIDI File Dump** (universal non-real-time `0x7E`, sub-ID
`0x07`) with the Sample Dump handshake. It is handled entirely in
`enomik_pd` (`src/patch_upload.cpp`); enomik's own config SysEx (`F0 7D …`)
is not involved. Upload messages never reach `[sysexin]`.

On by default (`Config::patchUpload`). Each client has a device ID
(`Config::deviceId`, default 0) and accepts messages addressed to it or to
`0x7F` (all). With several clients behind one dongle, give each its own ID.

Reference sender: [`tools/pd_upload.py`](tools/pd_upload.py)
(`pip install python-rtmidi`):

```bash
python3 tools/pd_upload.py --list
python3 tools/pd_upload.py --port enomik pd/synth.pd pd/main.pd
python3 tools/pd_upload.py --port dongle --device 3 pd/main.pd
```

### Messages

`dd` = device ID of the client (or `7F`), all values 7-bit.

| Message | Bytes | Direction |
|---------|-------|-----------|
| Header | `F0 7E dd 07 01 ss t1 t2 t3 t4 l0 l1 l2 l3 name… F7` | host → client |
| Data packet | `F0 7E dd 07 02 pp bb data… cc F7` | host → client |
| End of file | `F0 7E dd 7B pp F7` | host → client |
| Cancel | `F0 7E dd 7D pp F7` | either |
| ACK | `F0 7E dd 7F pp F7` | client → host |
| NAK | `F0 7E dd 7E pp F7` | client → host |

- **Header:** `ss` sender ID (ignored), `t1…t4` file type, 4 ASCII chars
  from the File Dump spec: `TEXT` for `.pd` and other text files, `BIN `
  (with a trailing space) for `.wav` and other binary files, `MIDI` for
  standard MIDI files. The client stores every type the same way (byte
  for byte) and does not check the field. `l0…l3` = file length in bytes,
  7 bits each, least significant first. `name` = file name in ASCII: letters, digits,
  `. _ - ~`, not starting with `.`, at most 40 characters, no paths. Max
  file size 1 MB (the partition limits it further).
- **Data packet:** `pp` packet number, starting at 0 for each file, wraps
  at 127. `bb` = number of encoded data bytes − 1 (at most 128 encoded bytes
  = 112 file bytes). `data` = file bytes packed 7-in-8: each group of up to 7
  bytes is sent as one byte holding their top bits (bit 6 = first byte's
  bit 7, bit 5 = second byte's, …) followed by the 7 bytes' low 7 bits.
  `cc` = XOR of all bytes from `7E` up to the last data byte, `& 0x7F`.
- Padding in the last packet is ignored (the header's length counts).

### Sequence

1. Host sends the header. Client answers ACK `pp = 0`, or Cancel (bad name,
   too large, cannot write).
2. For each packet: host sends it and waits for ACK with the same `pp`
   before the next. NAK means: send the packet whose number the NAK carries
   (after a checksum error that is the same packet, after an unexpected
   packet number the one the client is waiting for). Resending a
   packet that was already ACKed is answered with ACK again and not written
   twice.
3. Host sends End of file. The client checks the received length, replaces
   `<patchDir>/<name>` (written to a temp file until then) and answers ACK,
   or Cancel if bytes are missing.
4. If the file is the patch file (`main.pd`), the client reloads the patch.
   Send abstractions and other files first and `main.pd` last.

The client gives up a transfer after 10 s without messages (Cancel). A new
header during a transfer cancels the old one. Wait 2 s for each answer;
over ESP-NOW answers take a few ms, over USB less than 1 ms.

Flash writes stall both CPU cores briefly, so audio can click during an
upload.

## Limitations

- No externals (no dynamic loading). Built in: Pd vanilla plus `bob~`,
  `bonk~`, `choice`, `fiddle~`, `loop~`, `lrshift~`, `pique`, `sigmund~`.
  Not built: `pd~`, `stdout`.
- No GUI, one patch.
- `[file glob]` finds nothing; `[netsend]` / `[netreceive]` compile, but the
  client does not join a WiFi network (ESP-NOW only).
- Allocations above 4 KB (with `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096`:
  tables, delay lines, large arrays) go to PSRAM, which is slower than
  internal RAM. Heavy patches may need more blocks per buffer.
- On S2/S3 the enomik client turns native USB into a USB MIDI device, so USB
  serial logs stop when it starts. Use UART0 for logs.
