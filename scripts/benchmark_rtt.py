#!/usr/bin/env python3
"""Measures MIDI round-trip time, like pd-patches/benchmark_client-echo_rtt.pd.

Sends a CC (controller = counter % 127, value 24, channel 1) to the dongle and
waits until the client (client_echo) sends it back; the next one goes out
GAP ms later. Runs ROUNDS rounds of COUNT round trips (default 10 x 1000) and
writes each round, one round-trip time in ms per line with a header, to
benchmarks/<library version>/<transport>/<distance>m_obstacles-<n>/roundNN.txt
with its analysis (scripts/analyze_benchmark.py) next to it, plus summary.txt:
all rounds pooled and how median/p95/max vary between rounds. Rounds that
already exist are kept (an interrupted run continues with the missing ones).
--force measures all of them again: right before the first round (after the
start delay), it moves the existing rounds and summary into
previous-<date>-<time>/ inside the condition directory, so an interrupted --force run never mixes old and new
rounds. Resume such a run without --force.

Exit code 0 only when every round is there; 1 if rounds are missing (nothing
came back, or stopped with Ctrl-C), 2 for setup errors.

Timestamps are taken in the MIDI input callback with time.perf_counter(), so
the resolution is far finer than Pd's timer (~5.3 ms steps in the 0.10.8 data).

Usage: python3 scripts/benchmark_rtt.py --distance 1 --obstacles 0
Needs mido and python-rtmidi (e.g. scripts/wizard/.venv).
"""
import argparse
import gc
import os
import shutil
import statistics
import sys
import threading
import time
from pathlib import Path

import mido

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(Path(__file__).resolve().parent))
from analyze_benchmark import percentile, summarize_directory, write_analysis  # noqa: E402

CHANNEL = 0  # channel 1
VALUE = 24


def library_version():
    for line in (ROOT / "library.properties").read_text().splitlines():
        if line.startswith("version="):
            return line.split("=", 1)[1].strip()
    return "unknown"


def find(names, wanted):
    for name in names:
        if wanted in name:
            return name
    return None


def parse_args():
    p = argparse.ArgumentParser(description="Measure MIDI round-trip time through an echoing device.")
    p.add_argument("--distance", type=float, required=True, help="distance in m")
    p.add_argument("--obstacles", type=int, default=0)
    p.add_argument("--outdoor", action="store_true", help="default: indoor")
    p.add_argument("--transport", default="espnowMidi", help="subdirectory, e.g. espnowMidi, bleMidi, usbMidi")
    p.add_argument("--port", default="enomik3000_dongle", help="MIDI port name (substring)")
    p.add_argument("--in-port", help="input port name if it differs from --port")
    p.add_argument("--count", type=int, default=1000, help="round trips per round")
    p.add_argument("--rounds", type=int, default=10)
    p.add_argument("--round-pause", type=float, default=2.0, help="seconds between rounds")
    p.add_argument("--gap-ms", type=float, default=10.0, help="pause after each echo")
    p.add_argument("--timeout-ms", type=float, default=1000.0, help="count as lost after this")
    p.add_argument("--start-delay", type=float, default=5.0, help="seconds before the first message")
    p.add_argument("--out", type=Path,
                   help="output directory (default: benchmarks/<version>/<transport>/<d>m_obstacles-<n>)")
    p.add_argument("--force", action="store_true",
                   help="measure all rounds again; existing ones are moved to previous-<date>-<time>/")
    return p.parse_args()


def measure(args, port_out, expected, received, echoed, label):
    values, lost = [], 0
    gc.disable()
    try:
        for i in range(args.count):
            control = i % 127
            echoed.clear()
            expected["control"] = control
            msg = mido.Message("control_change", channel=CHANNEL, control=control, value=VALUE)
            sent = time.perf_counter()
            port_out.send(msg)
            if echoed.wait(args.timeout_ms / 1000):
                values.append((received["t"] - sent) * 1000)
            else:
                lost += 1
            expected["control"] = None
            if (i + 1) % 250 == 0:
                print(f"{label} {i + 1}/{args.count}: median {statistics.median(values):.2f} ms, lost {lost}"
                      if values else f"{label} {i + 1}/{args.count}: lost {lost}", flush=True)
            time.sleep(args.gap_ms / 1000)
    finally:
        gc.enable()
    return values, lost


def move_aside(out_dir):
    """Move existing round files and summary.txt into out_dir/previous-<date>-<time>/."""
    old = [p for p in out_dir.glob("round*") if p.is_file()]
    old += [p for p in [out_dir / "summary.txt"] if p.exists()]
    if not old:
        return None
    target = out_dir / time.strftime("previous-%Y%m%d-%H%M%S")
    n = 1
    while target.exists():
        n += 1
        target = out_dir / time.strftime(f"previous-%Y%m%d-%H%M%S-{n}")
    target.mkdir()
    for p in old:
        shutil.move(str(p), str(target / p.name))
    return target


def main():
    args = parse_args()
    version = library_version()
    distance = f"{args.distance:g}"
    out_dir = args.out or (ROOT / "benchmarks" / version / args.transport
                           / f"{distance}m_obstacles-{args.obstacles}")
    width = max(2, len(str(args.rounds)))
    rounds = [out_dir / f"round{n:0{width}d}.txt" for n in range(1, args.rounds + 1)]
    todo = rounds if args.force else [r for r in rounds if not r.exists()]
    if not todo:
        print(f"all {args.rounds} rounds exist in {out_dir} (--force to measure again)")
        print(f"written: {summarize_directory(out_dir)}")
        return 0
    if len(todo) < len(rounds):
        print(f"{len(rounds) - len(todo)} of {args.rounds} rounds exist, measuring the other {len(todo)}")

    # Opening a CoreMIDI port sometimes hangs for good.
    def give_up():
        print("ERROR: opening the MIDI ports hangs (CoreMIDI). Run 'killall MIDIServer' "
              "(no MIDI app open) and start again.", flush=True)
        os._exit(2)

    def stop(text):
        print(text, flush=True)
        os._exit(1)  # os._exit: closing CoreMIDI ports at exit can hang

    timer = threading.Timer(10, give_up)
    timer.daemon = True  # never keeps the script alive on its own
    timer.start()
    try:
        out_name = find(mido.get_output_names(), args.port)
        in_name = find(mido.get_input_names(), args.in_port or args.port)
    except KeyboardInterrupt:
        timer.cancel()
        stop("stopped while looking for the MIDI ports; nothing measured or moved")
    if not out_name or not in_name:
        timer.cancel()
        print(f"ERROR: MIDI ports not found (out: {out_name}, in: {in_name})")
        print(f"outputs: {mido.get_output_names()}\ninputs: {mido.get_input_names()}")
        return 2

    expected = {"control": None}
    received = {"t": 0.0}
    echoed = threading.Event()

    def on_message(msg):
        now = time.perf_counter()
        if (msg.type == "control_change" and msg.channel == CHANNEL and msg.value == VALUE
                and msg.control == expected["control"] and not echoed.is_set()):
            received["t"] = now
            echoed.set()

    try:
        port_in = mido.open_input(in_name, callback=on_message)  # noqa: F841 (keep it open)
        port_out = mido.open_output(out_name)
    except KeyboardInterrupt:
        timer.cancel()
        stop("stopped while opening the MIDI ports; nothing measured or moved")
    timer.cancel()
    print(f"{len(todo)} x {args.count} round trips via '{out_name}' -> '{in_name}' into {out_dir}, "
          f"starting in {args.start_delay:g} s", flush=True)
    try:
        time.sleep(args.start_delay)
    except KeyboardInterrupt:
        stop("stopped before the first round; nothing measured or moved")
    if args.force and out_dir.is_dir():
        moved = move_aside(out_dir)
        if moved:
            print(f"--force: moved the existing rounds to {moved}", flush=True)

    out_dir.mkdir(parents=True, exist_ok=True)
    stopped = False
    try:
        for i, path in enumerate(todo):
            if i:
                time.sleep(args.round_pause)
            n = rounds.index(path) + 1
            label = f"round {n}/{args.rounds}"
            values, lost = measure(args, port_out, expected, received, echoed, label)
            if not values:
                print(f"{label}: nothing came back", flush=True)
                continue
            header = [
                f"distance: {distance} m",
                f"obstacles: {args.obstacles}",
                f"indoor: {0 if args.outdoor else 1}",
                f"library: {version}",
                f"port: {out_name}",
                f"date: {time.strftime('%Y-%m-%d %H:%M:%S')}",
                "timer: python perf_counter",
                f"gap: {args.gap_ms:g} ms",
                f"round: {n}/{args.rounds}",
                f"lost: {lost}",
            ]
            path.write_text("\n".join(header) + "\n\n" + "\n".join(f"{v:.3f}" for v in values) + "\n")
            write_analysis(path, values)
            s = sorted(values)
            print(f"{label}: median {statistics.median(s):.2f} p95 {percentile(s, 0.95):.2f} "
                  f"max {s[-1]:.2f} ms, {len(s)} samples, lost {lost}", flush=True)
    except KeyboardInterrupt:
        stopped = True
        print("stopped", flush=True)

    missing = [r.name for r in rounds if not r.exists()]
    if len(missing) == len(rounds):
        print("no round finished, nothing written" if stopped else "ERROR: nothing came back", flush=True)
        os._exit(1)
    summary = summarize_directory(out_dir)
    print(f"written: {out_dir}/round*.txt, round*_analysis.txt\n         {summary}")
    for line in summary.read_text().splitlines():
        if line.startswith(("samples:", "lost:", "median:", "p95:", "p99.9:", "max:")):
            print(f"  {line}")
    if missing:
        again = "the same command without --force" if args.force else "the same command"
        print(f"incomplete: {', '.join(missing)} missing; run {again} again to measure them", flush=True)
    sys.stdout.flush()
    os._exit(1 if missing else 0)  # os._exit: closing CoreMIDI ports at exit can hang


if __name__ == "__main__":
    sys.exit(main())
