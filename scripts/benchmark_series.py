#!/usr/bin/env python3
"""Runs a whole round-trip benchmark series: several distances, ROUNDS rounds each.

For each condition it asks you to place the client, then runs
scripts/benchmark_rtt.py (10 x 1000 round trips by default) into
benchmarks/<version>/<transport>/<distance>m_obstacles-<n>/. Conditions with
all their rounds are skipped; after Ctrl-C or a failure, run the same command
again and it continues with the missing rounds. --force measures conditions
again; their old rounds are moved to previous-<date>-<time>/ first. To resume
an interrupted --force series, run it again without --force.

Exit code 0 only when every listed condition has all its rounds afterwards;
1 when one was skipped (s), not measured (q, Ctrl-C) or is incomplete. At the end it writes the comparison with all versions to
benchmarks/<version>/<transport>/comparison.md (scripts/compare_benchmarks.py).

Usage:
  python3 scripts/benchmark_series.py                    # default series below
  python3 scripts/benchmark_series.py 1:0 3:0 5:1 10:2   # distance:obstacles
  python3 scripts/benchmark_series.py 5:1 --force        # measure again
  python3 scripts/benchmark_series.py --yes              # don't wait for Enter

Options after `--` go to benchmark_rtt.py, e.g. `-- --outdoor --gap-ms 20`.
Setup: dongle on USB, client running client_echo. Needs mido and
python-rtmidi: scripts/wizard/run.sh creates scripts/wizard/.venv with them.
"""
import argparse
import subprocess
import sys
from pathlib import Path

from benchmark_rtt import ROOT, library_version

# distance in m, obstacles
DEFAULT_SERIES = ["1:0", "3:0", "5:1", "10:1", "15:2", "20:0"]


def parse_condition(text):
    try:
        distance, obstacles = text.split(":")
        return float(distance), int(obstacles)
    except ValueError:
        raise argparse.ArgumentTypeError(f"expected distance:obstacles, e.g. 5:1, got '{text}'")


def main():
    sys.stdout.reconfigure(line_buffering=True)
    argv = sys.argv[1:]
    extra = []
    if "--" in argv:
        extra = argv[argv.index("--") + 1:]
        argv = argv[:argv.index("--")]
    p = argparse.ArgumentParser(description="Run a round-trip benchmark series over several distances.",
                                epilog="Options after -- go to benchmark_rtt.py.")
    p.add_argument("conditions", nargs="*", type=parse_condition,
                   help=f"distance:obstacles (default: {' '.join(DEFAULT_SERIES)})")
    p.add_argument("--rounds", type=int, default=10)
    p.add_argument("--count", type=int, default=1000, help="round trips per round")
    p.add_argument("--transport", default="espnowMidi")
    p.add_argument("--force", action="store_true", help="measure conditions again (old rounds are moved to previous-<date>-<time>/)")
    p.add_argument("--yes", action="store_true", help="don't wait for Enter before each condition")
    args = p.parse_args(argv)
    conditions = args.conditions or [parse_condition(c) for c in DEFAULT_SERIES]

    version = library_version()
    base = ROOT / "benchmarks" / version / args.transport
    rtt = Path(__file__).resolve().parent / "benchmark_rtt.py"
    print(f"library {version}, {len(conditions)} conditions, {args.rounds} x {args.count} round trips each")

    width = max(2, len(str(args.rounds)))

    def complete(name):
        return all((base / name / f"round{n:0{width}d}.txt").exists() for n in range(1, args.rounds + 1))

    results = []
    stop_reason = None  # set when the series ends early
    for distance, obstacles in conditions:
        name = f"{distance:g}m_obstacles-{obstacles}"
        if complete(name) and not args.force:
            print(f"\n{name}: all {args.rounds} rounds done, skipped (--force to measure again)")
            results.append((name, "done already"))
            continue
        if not args.yes:
            try:
                answer = input(f"\nPlace the client {distance:g} m away, {obstacles} obstacle(s). "
                               "Enter = start, s = skip, q = quit: ").strip().lower()
            except (KeyboardInterrupt, EOFError):
                answer = "q"
            if answer == "q":
                stop_reason = "quit"
                break
            if answer == "s":
                results.append((name, "skipped by you"))
                continue
        cmd = [sys.executable, str(rtt), "--distance", f"{distance:g}", "--obstacles", str(obstacles),
               "--rounds", str(args.rounds), "--count", str(args.count), "--transport", args.transport]
        if args.force:
            cmd.append("--force")
        proc = subprocess.Popen(cmd + extra)
        try:
            code = proc.wait()
        except KeyboardInterrupt:  # Ctrl-C reaches both; let benchmark_rtt.py save its rounds
            proc.wait()
            results.append((name, "stopped, incomplete"))
            stop_reason = "stopped"
            break
        results.append((name, "ok" if code == 0 else f"incomplete (exit {code})"))
        if code != 0 and not args.yes:
            try:
                go_on = input("Not all rounds done. Continue with the next condition? [y/N] ").strip().lower()
            except (KeyboardInterrupt, EOFError):
                go_on = ""
            if go_on != "y":
                stop_reason = "stopped"
                break
    handled = {name for name, _ in results}
    for distance, obstacles in conditions:
        name = f"{distance:g}m_obstacles-{obstacles}"
        if name not in handled:
            results.append((name, "done already" if complete(name) and not args.force
                            else f"not measured ({stop_reason})"))

    print("\nseries:")
    for name, status in results:
        print(f"  {name}: {status}")
    if base.is_dir():
        compare = Path(__file__).resolve().parent / "compare_benchmarks.py"
        subprocess.call([sys.executable, str(compare), "--transport", args.transport,
                         "--out", str(base / "comparison.md")])
    # 0 only when every listed condition now has all its rounds
    return 0 if all(status in ("ok", "done already") for _, status in results) else 1


if __name__ == "__main__":
    sys.exit(main())
