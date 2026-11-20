# enomik::Client + Pure Data on the Waveshare ESP32-S3-AUDIO-Board

[`client_pd`](../client_pd) for the
[Waveshare ESP32-S3-AUDIO-Board](https://www.waveshare.com/wiki/ESP32-S3-AUDIO-Board):
a Pd patch with `[dac~]` on the on-board speaker, `[adc~]` on the two
microphones, and Pd's MIDI objects on ESP-NOW MIDI (and USB MIDI).

Everything except the audio setup is shared with `client_pd`: libpd and the
enomik component (`../client_pd/components`), and the Pd runtime, MIDI bridge
and `app_main` (`../client_pd/main`). See the
[client_pd README](../client_pd/README.md) for the MIDI mapping, patches and
limitations.

No board support package is used. `main/pd_audio_waveshare.cpp` sets up the
board itself, with only the chip drivers from
[`espressif/esp_codec_dev`](https://components.espressif.com/components/espressif/esp_codec_dev).

## Hardware

| Part | Connection | Pd |
|------|------------|----|
| ES8311 DAC → NS4150B amp → speaker | I2C 0x18, I2S DOUT GPIO16 | `[dac~ 1 2]` (mono amp) |
| ES7210 ADC, MIC1 + MIC2 | I2C 0x40, I2S DIN GPIO15 | `[adc~ 1 2]` |
| TCA9555 I/O expander, EXIO8 = amp enable | I2C 0x20 | – |
| Shared I2S clocks | MCLK 12, BCLK 13, WS 14 | 48 kHz, 16 bit, ESP32 is master |
| I2C | SDA 11, SCL 10 | – |

ESP32-S3R8 (8 MB octal PSRAM), 16 MB flash. Partitions: 4 MB app, 2 MB `pd`
LittleFS.

Only port 1 of the TCA9555 is written (EXIO8 to output, high). Port 0 is left
alone: EXIO6/7 switch the USB/camera mux, and driving them can break USB
flashing.

## Build and flash

```bash
. ~/.espressif/v6.0.1/esp-idf/export.sh    # ESP-IDF 6.0
cd examples_idf/client_pd_waveshare_audio_s3
idf.py set-target esp32s3
idf.py build
idf.py -p PORT flash monitor
```

The first build clones libpd into `../client_pd/components/libpd/libpd/`
(shared with `client_pd`).

### Pd DSP placement

The default configuration enables `CONFIG_LIBPD_IRAM_DSP`: 55 common libpd DSP
routine variants run from internal instruction RAM. This adds about 10.1 KiB of
internal SRAM use in the verified build. For an existing configuration, enable
**Component config → libpd → Place common Pd DSP routines in IRAM** in
`idf.py menuconfig`. Disable it to compare with normal flash placement.
See [the libpd component documentation](../../components/libpd/README.md#optional-iram-placement)
for the routine selection, memory measurements and placement audit.

### USB

The board has one USB-C port (GPIO19/20). The enomik client turns it into a
USB MIDI device (TinyUSB) when it starts, so:

- Serial logs over USB stop at that point. For full logs, connect a USB-UART
  adapter to UART0 (TX GPIO43, RX GPIO44).
- To flash again, hold **BOOT**, press **RESET**, release **BOOT**, then
  `idf.py -p PORT flash`.

## Configuration (`idf.py menuconfig` → client_pd (Waveshare ESP32-S3-AUDIO))

| Option | Default |
|--------|---------|
| Sample rate | 48000 |
| Pd blocks per audio buffer | 2 (128 frames, 2.7 ms) |
| CPU core for the Pd task | 1 |
| Speaker volume (ES8311, 0–100) | 70 |
| Microphone gain (ES7210, dB) | 30 |

## Patch

`pd/` is flashed with the firmware: `main.pd` + `voice.pd`, an 8-voice
polyphonic saw synth (CC 1 = filter cutoff); the microphone level goes out as
CC 20 on channel 1. Note that the mics hear the speaker.

`patches/` holds patches to upload later over SysEx (enomik web app `#/pd`
or `components/enomik_pd/tools/pd_upload.py`), each in its own folder:

- [`kalimba_groove/`](patches/kalimba_groove): synth + kalimba sampler (CC0
  sample) + 16-step sequencer + tempo-synced delay.

## Known issues

- Users report that the ES7210 sometimes does not start after a cold power-on
  and only works after a reset. No fix is known. If `[adc~]` stays silent,
  press RESET.
- Not used here: LCD, camera, SD card, the RGB LED ring (GPIO38), the keys
  (TCA9555 EXIO9–11).

Pin and address sources: Waveshare schematic v1.1 and demo code, as collected
in [this hardware reference](https://github.com/MichalZaniewicz/esphome-waveshare-esp32-s3-audio-va/blob/main/docs/HARDWARE.md).
