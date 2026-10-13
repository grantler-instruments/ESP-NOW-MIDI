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
| [0.10.7](0.10.7/) | 2026-03 | Round-trip time: ESP-NOW MIDI by distance, BLE MIDI, RTP-MIDI, USB MIDI |
| [0.19.0](0.19.0/) | 2026-10 | 10 h soak test of the ESP-NOW send path |

Round-trip files are analyzed with
`python3 scripts/analyze_benchmark.py <file>.txt`, which writes
`<file>_analysis.txt` next to it. Summary: [docs/benchmarks.md](../docs/benchmarks.md).
