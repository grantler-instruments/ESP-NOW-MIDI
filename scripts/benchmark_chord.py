#!/usr/bin/env python3
"""Measures how chords travel: several note ons sent at once, echoed back.

Sends a major triad (or --notes N stacked notes) as note ons in one go to the
dongle and waits until the client (client_echo) has sent all of them back,
then the note offs. The next chord goes out GAP ms later; the root moves up a
semitone each time (C2..B5). Per chord it records:

- complete: time until the last note of the chord came back (ms)
- first:    time until the first note came back (ms)
- spread:   last minus first echo, how far the chord is smeared (ms)

Writes `complete` (one value per line) to
benchmarks/<library version>/<transport>/<distance>m_obstacles-<n>_chord<N>.txt
and `spread` to ..._chord<N>_spread.txt, each with its analysis
(scripts/analyze_benchmark.py).

Usage: python3 scripts/benchmark_chord.py --distance 1 --obstacles 0
Needs mido and python-rtmidi (e.g. scripts/wizard/.venv).
"""
import argparse
import gc
import os
import statistics
import sys
import threading
import time
from pathlib import Path

import mido

sys.path.insert(0, str(Path(__file__).resolve().parent))
from analyze_benchmark import percentile, write_analysis  # noqa: E402
from benchmark_rtt import ROOT, find, library_version  # noqa: E402

CHANNEL = 0  # channel 1
VELOCITY = 100
INTERVALS = [0, 4, 7, 12, 16, 19]  # major triad, then the next octave


def parse_args():
    p = argparse.ArgumentParser(description="Measure how chords come back through an echoing device.")
    p.add_argument("--distance", type=float, required=True, help="distance in m")
    p.add_argument("--obstacles", type=int, default=0)
    p.add_argument("--outdoor", action="store_true", help="default: indoor")
    p.add_argument("--notes", type=int, default=3, choices=range(2, len(INTERVALS) + 1),
                   help="notes per chord (default 3)")
    p.add_argument("--transport", default="espnowMidi", help="subdirectory, e.g. espnowMidi, bleMidi, usbMidi")
    p.add_argument("--port", default="enomik3000_dongle", help="MIDI port name (substring)")
    p.add_argument("--in-port", help="input port name if it differs from --port")
    p.add_argument("--count", type=int, default=1000, help="chords")
    p.add_argument("--gap-ms", type=float, default=10.0, help="pause after each chord's note offs came back")
    p.add_argument("--timeout-ms", type=float, default=1000.0, help="count missing notes as lost after this")
    p.add_argument("--start-delay", type=float, default=5.0, help="seconds before the first chord")
    p.add_argument("--out", type=Path, help="output file (default: benchmarks/<version>/<transport>/...)")
    p.add_argument("--force", action="store_true", help="overwrite existing output files")
    return p.parse_args()


def fmt(values):
    s = sorted(values)
    return f"median {statistics.median(s):.2f} p95 {percentile(s, 0.95):.2f} max {s[-1]:.2f} ms"


def main():
    args = parse_args()
    version = library_version()
    distance = f"{args.distance:g}"
    name = f"{distance}m_obstacles-{args.obstacles}_chord{args.notes}"
    out = args.out or ROOT / "benchmarks" / version / args.transport / f"{name}.txt"
    spread_out = out.with_name(f"{out.stem}_spread.txt")
    for path in (out, spread_out):
        if path.exists() and not args.force:
            print(f"ERROR: {path} exists (use --force to overwrite)")
            return 2

    # Opening a CoreMIDI port sometimes hangs for good.
    def give_up():
        print("ERROR: opening the MIDI ports hangs (CoreMIDI). Run 'killall MIDIServer' "
              "(no MIDI app open) and start again.", flush=True)
        os._exit(2)

    timer = threading.Timer(10, give_up)
    timer.start()
    out_name = find(mido.get_output_names(), args.port)
    in_name = find(mido.get_input_names(), args.in_port or args.port)
    if not out_name or not in_name:
        timer.cancel()
        print(f"ERROR: MIDI ports not found (out: {out_name}, in: {in_name})")
        print(f"outputs: {mido.get_output_names()}\ninputs: {mido.get_input_names()}")
        return 2

    lock = threading.Lock()
    state = {"type": None, "waiting": set(), "times": {}}
    done = threading.Event()

    def on_message(msg):
        now = time.perf_counter()
        # Note off may arrive as note on with velocity 0
        kind = "note_off" if msg.type == "note_off" or (msg.type == "note_on" and msg.velocity == 0) else msg.type
        if kind not in ("note_on", "note_off") or msg.channel != CHANNEL:
            return
        if kind == "note_on" and msg.velocity != VELOCITY:
            return
        with lock:
            if kind == state["type"] and msg.note in state["waiting"]:
                state["waiting"].discard(msg.note)
                state["times"][msg.note] = now
                if not state["waiting"]:
                    done.set()

    def send_all(kind, notes):
        with lock:
            state["type"] = kind
            state["waiting"] = set(notes)
            state["times"] = {}
        done.clear()
        velocity = VELOCITY if kind == "note_on" else 0
        sent = time.perf_counter()
        for note in notes:
            port_out.send(mido.Message(kind, channel=CHANNEL, note=note, velocity=velocity))
        complete = done.wait(args.timeout_ms / 1000)
        with lock:
            times = dict(state["times"])
            state["type"] = None
        return sent, complete, times

    port_in = mido.open_input(in_name, callback=on_message)  # noqa: F841 (keeps the callback alive)
    port_out = mido.open_output(out_name)
    timer.cancel()
    print(f"{args.count} chords of {args.notes} notes via '{out_name}' -> '{in_name}', "
          f"starting in {args.start_delay:g} s", flush=True)
    time.sleep(args.start_delay)

    complete_ms, first_ms, spread_ms = [], [], []
    lost_chords = lost_notes = lost_offs = 0
    gc.disable()
    try:
        for i in range(args.count):
            root = 36 + i % 48
            notes = [root + interval for interval in INTERVALS[:args.notes]]
            sent, complete, times = send_all("note_on", notes)
            if complete:
                arrivals = sorted(times.values())
                complete_ms.append((arrivals[-1] - sent) * 1000)
                first_ms.append((arrivals[0] - sent) * 1000)
                spread_ms.append((arrivals[-1] - arrivals[0]) * 1000)
            else:
                lost_chords += 1
                lost_notes += args.notes - len(times)
            _, offs_complete, offs = send_all("note_off", notes)
            if not offs_complete:
                lost_offs += args.notes - len(offs)
            if (i + 1) % 100 == 0:
                summary = f"complete {fmt(complete_ms)}, " if complete_ms else ""
                print(f"{i + 1}/{args.count}: {summary}lost chords {lost_chords}", flush=True)
            time.sleep(args.gap_ms / 1000)
    except KeyboardInterrupt:
        print("stopped")
    gc.enable()

    if not complete_ms:
        print("ERROR: no chord came back complete", flush=True)
        os._exit(1)

    out.parent.mkdir(parents=True, exist_ok=True)
    header = [
        f"distance: {distance} m",
        f"obstacles: {args.obstacles}",
        f"indoor: {0 if args.outdoor else 1}",
        f"library: {version}",
        f"port: {out_name}",
        f"date: {time.strftime('%Y-%m-%d %H:%M:%S')}",
        "timer: python perf_counter",
        f"chord: {args.notes} notes ({', '.join(str(i) for i in INTERVALS[:args.notes])} semitones)",
        f"gap: {args.gap_ms:g} ms",
        f"lost chords: {lost_chords} ({lost_notes} note ons, {lost_offs} note offs missing)",
    ]
    for path, label, values in ((out, "time until the whole chord came back", complete_ms),
                                (spread_out, "last minus first echo of a chord", spread_ms)):
        path.write_text("\n".join(header + [f"values: {label}"]) + "\n\n"
                        + "\n".join(f"{v:.3f}" for v in values) + "\n")
        write_analysis(path, values)

    print(f"whole chord: {fmt(complete_ms)}")
    print(f"first note:  {fmt(first_ms)}")
    print(f"spread:      {fmt(spread_ms)}")
    print(f"{len(complete_ms)} chords complete, {lost_chords} lost "
          f"({lost_notes} note ons, {lost_offs} note offs missing)")
    print(f"written: {out}\n         {spread_out}\n         (+ *_analysis.txt)")
    sys.stdout.flush()
    os._exit(0)  # closing CoreMIDI ports at exit can hang


if __name__ == "__main__":
    sys.exit(main())
