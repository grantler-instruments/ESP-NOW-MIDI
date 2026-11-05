# 0.19.1 round-trip benchmarks

Measured 2026-11-04 with the 0.19.1 code before its release: uncommitted
changes on top of e1b891e. Indoors, macOS host. The round files record the
version in `library.properties` on the computer, not the firmware on the
boards.

Setup: computer, USB, dongle (`enomik3000_dongle`), ESP-NOW, client running
`client_echo`, back the same way. `scripts/benchmark_rtt.py` sends a control
change and waits for it to come back, timed with Python's `perf_counter`
(sub-millisecond resolution, unlike the Pd timer of the 0.10.8 runs).

Each condition is 10 rounds of 1000 round trips (`scripts/benchmark.sh`):

- `espnowMidi/1m_obstacles-0/`
- `espnowMidi/3m_obstacles-0/`
- `espnowMidi/5m_obstacles-1/`

Each holds `round01.txt` ... `round10.txt` (one round-trip time in ms per line,
header with setup and losses), a `*_analysis.txt` per round and `summary.txt`
(all rounds pooled, jitter, spread between rounds).
[`espnowMidi/comparison.md`](espnowMidi/comparison.md) compares them with 0.10.8.

## Results

| | 1 m, 0 obstacles | 3 m, 0 obstacles | 5 m, 1 obstacle |
| --- | --- | --- | --- |
| median | 4.91 ms | 4.93 ms | 4.90 ms |
| p95 | 13.99 ms | 14.37 ms | 13.77 ms |
| p99.9 | 29.67 ms | 29.65 ms | 29.65 ms |
| max | 39.96 ms | 31.60 ms | 32.22 ms |
| lost | 0 / 10,000 | 0 / 10,000 | 0 / 10,000 |

- Distance up to 5 m and one obstacle make no measurable difference.
- About 9 in 10 round trips land within 2 ms of the median. The rest form a
  small slow group (about 6 % at 12–20 ms, 1–2 % at 20–30 ms) capped near
  29.6 ms; only a few exceed it. The cause of that cap is not known yet.
- Against 0.10.8 at 1 m and 3 m (same conditions) and at 5 m (0.19.1 with
  1 obstacle, 0.10.8 with none): median 6–11 ms down to 4.9 ms, p95 32.7 ms
  down to about 14 ms, standard deviation about 10 ms down to about 3.5 ms,
  messages more than 10 ms over the median from about a third to about 4 %.
