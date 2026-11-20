# kalimba_groove

A Pd patch for the Pd client on the Waveshare ESP32-S3-AUDIO-Board: a
polyphonic synth, a kalimba sampler, a 16-step sequencer and a tempo-synced
delay. Upload all four files with the enomik web app (`#/pd`) or
`components/enomik_pd/tools/pd_upload.py`. `main.pd` goes last, which
reloads the patch. The sequencer starts by itself.

| File | |
|------|---|
| `main.pd` | MIDI in/out, mixer, delay, sequencer, loads the sample |
| `voice.pd` | one synth voice: saw → low-pass → envelope |
| `sampler.pd` | one kalimba voice: plays `kalimba.wav`, pitched by note |
| `kalimba.wav` | the sample: mono, 16 bit, 48 kHz, 1.8 s, 169 KB |

## Playing it

| MIDI | Does |
|------|------|
| Notes, channel 1 | 8-voice saw synth |
| Notes, channel 2 | kalimba, 4 voices, D4 (62) = original pitch |
| CC 1 | synth filter cutoff (300 Hz – 7.9 kHz) |
| CC 2 | delay mix (0 – 1) |
| CC 3 | delay feedback (0 – 0.9) |
| CC 20 | sequencer: ≥ 64 runs, < 64 stops |
| CC 21 | tempo: 60 + value BPM (60 – 187) |
| Start / Stop | starts / stops the sequencer |
| Notes out, channel 3 | each sequencer step, so other enomik devices can follow |

Defaults at load: 110 BPM, delay 3/16 note, mix 0.3, feedback 0.45, cutoff
2.5 kHz, sequencer running.

The step pattern is the array `kg-pattern` in `main.pd` (16 MIDI notes, 0 =
rest). Edit it in Pd (open the array) and save, or change the `#A 0 …` line.

## Notes

- All voices of `[clone]` compute all the time, playing or not. If the audio
  crackles on the board, lower the voice counts (`poly 8 1` / `clone … 8`,
  `poly 4 1` / `clone … 4`) or raise the Pd blocks per buffer in menuconfig.
- The sampler assumes 48 kHz (the board's rate) for the playback length.
- `[s~ kg-out]` taps the master output for testing; it costs next to nothing.

## Sample credit

`kalimba.wav` is a trimmed, mono, normalized version of
`MBira3_pluck_Main_D4_k19_50_100_rr2.wav` from the
[Versilian Community Sample Library (VCSL)](https://github.com/sgossner/VCSL)
by Versilian Studios, released under
[CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/) (public domain,
no attribution required).
