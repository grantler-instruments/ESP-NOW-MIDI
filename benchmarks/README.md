# Benchmarks

Raw measurements and their analysis, one directory per library version
(`version` in `library.properties`) the boards were running:

```
benchmarks/<version>/<transport or test>/
```

Runs on unreleased code go under the upcoming version; note the commit in that
version's README. Each version directory has a README with date, setup and
what was measured.

| Version | Date | Contents |
| --- | --- | --- |
| [0.10.8](0.10.8/) | 2026-03 | Round-trip time: ESP-NOW MIDI by distance, BLE MIDI, RTP-MIDI, USB MIDI |
| [0.19.1](0.19.1/) | 2026-11 | ESP-NOW MIDI round-trip time and jitter, 1–5 m, 10 × 1000 per distance |

## Round-trip series

Setup: dongle on USB, client running `client_echo`. Then:

```sh
scripts/benchmark.sh                   # default series (see benchmark_series.py)
scripts/benchmark.sh 1:0 3:0 5:1 10:2  # distance:obstacles
```

The first run creates `scripts/wizard/.venv` with `mido` and `python-rtmidi`.
For each condition it asks you to place the client (Enter = start, s = skip,
q = quit), then runs 10 rounds of 1000 round trips. Conditions already measured
are skipped, so after an interruption just run the same command again.
`--force` measures them again and first moves their old rounds into
`previous-<date>-<time>/` inside the condition directory; to resume an
interrupted `--force` run, run it again without `--force`. Options after `--`
go to `benchmark_rtt.py`,
e.g. `scripts/benchmark.sh 20:0 -- --outdoor`.

Results, per condition:

```
benchmarks/<version>/espnowMidi/5m_obstacles-1/
  round01.txt ... round10.txt                    raw round trips (ms), header with setup and losses
  round01_analysis.txt ... round10_analysis.txt  per round
  summary.txt                                    all rounds pooled: latency, jitter, spread between rounds
benchmarks/<version>/espnowMidi/comparison.md    every condition, compared with older versions
```

Single condition: `scripts/wizard/.venv/bin/python scripts/benchmark_rtt.py
--distance 5 --obstacles 1` (`--rounds`, `--count`, `--transport`, `--port`;
`--help` for the rest). Recompute a summary after editing round files:
`python3 scripts/analyze_benchmark.py <condition directory>`. Comparison only:
`python3 scripts/compare_benchmarks.py [versions...] [--out file.md]`.

## Chords

`scripts/benchmark_chord.py` (same options as `benchmark_rtt.py`, plus
`--notes`, default 3) sends the notes of a chord at once and records when the
whole chord came back (`..._chord3.txt`) and how far apart its first and last
note arrived (`..._chord3_spread.txt`). It still writes one run per file, next
to the condition directories, not in rounds.

## Analysis

`python3 scripts/analyze_benchmark.py <file>.txt` writes `<file>_analysis.txt`
next to a single run; on a condition directory it rewrites `summary.txt`.
Overview of the results: [docs/benchmarks.md](../docs/benchmarks.md).
