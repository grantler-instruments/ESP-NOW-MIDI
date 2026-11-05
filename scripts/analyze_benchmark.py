#!/usr/bin/env python3
"""Analyze benchmark results.

  analyze_benchmark.py <file>.txt   writes <file>_analysis.txt next to it
  analyze_benchmark.py <directory>  (re)writes <directory>/summary.txt from its
                                    round*.txt files, plus each round's analysis
"""

from __future__ import annotations

import argparse
from pathlib import Path
import statistics


def percentile(sorted_values: list[float], p: float) -> float:
    """Compute percentile using linear interpolation (NumPy-style)."""
    if not sorted_values:
        raise ValueError("Cannot compute percentile of empty data")
    if len(sorted_values) == 1:
        return sorted_values[0]

    rank = (len(sorted_values) - 1) * p
    low = int(rank)
    high = min(low + 1, len(sorted_values) - 1)
    weight = rank - low
    return sorted_values[low] * (1.0 - weight) + sorted_values[high] * weight


def mad(values: list[float], median_value: float) -> float:
    """Median absolute deviation (robust jitter metric)."""
    return statistics.median([abs(v - median_value) for v in values])


def parse_values(file_path: Path) -> list[float]:
    values: list[float] = []
    with file_path.open("r", encoding="utf-8") as f:
        for raw_line in f:
            line = raw_line.strip()
            if not line:
                continue
            try:
                values.append(float(line))
            except ValueError:
                # Skip non-numeric metadata/header lines.
                continue
    return values


def write_analysis(file_path: Path, values: list[float]) -> Path:
    output_path = file_path.with_name(f"{file_path.stem}_analysis.txt")
    sorted_values = sorted(values)

    mean_value = statistics.mean(values)
    median_value = statistics.median(values)
    min_value = sorted_values[0]
    max_value = sorted_values[-1]
    # Tail behavior
    p95_value = percentile(sorted_values, 0.95)
    p99_value = percentile(sorted_values, 0.99)
    # Jitter-focused metrics
    sigma_sample = statistics.stdev(values) if len(values) > 1 else 0.0
    sigma_population = statistics.pstdev(values) if values else 0.0
    p25_value = percentile(sorted_values, 0.25)
    p75_value = percentile(sorted_values, 0.75)
    iqr_value = p75_value - p25_value
    mad_value = mad(values, median_value)
    # Peak-to-peak spread
    p2p_value = max_value - min_value

    lines = [
        f"source_file: {file_path.name}",
        f"samples: {len(values)}",
        f"min: {min_value:.6f}",
        f"max: {max_value:.6f}",
        f"mean: {mean_value:.6f}",
        f"median: {median_value:.6f}",
        f"p95: {p95_value:.6f}",
        f"p99: {p99_value:.6f}",
        # New jitter / variability fields (appended, so old parsers keep working)
        f"sigma_sample: {sigma_sample:.6f}",
        f"sigma_population: {sigma_population:.6f}",
        f"p25: {p25_value:.6f}",
        f"p75: {p75_value:.6f}",
        f"iqr: {iqr_value:.6f}",
        f"mad: {mad_value:.6f}",
        f"peak_to_peak: {p2p_value:.6f}",
    ]
    output_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return output_path


JITTER_BUCKETS = [(0, 8), (8, 12), (12, 20), (20, 30), (30, float("inf"))]


def jitter_metrics(series: list[list[float]]) -> dict[str, float]:
    """Jitter beyond sigma/IQR, over one or more runs (each in send order).

    late_*: share (%) of messages more than 2/5/10 ms over the median.
    consecutive_delta: mean |difference| between consecutive round trips.
    bucket_*: share (%) of round trips per range in ms.
    """
    pooled = [v for values in series for v in values]
    median_value = statistics.median(pooled)
    n = len(pooled)
    deltas = [abs(a - b) for values in series for a, b in zip(values, values[1:])]
    metrics = {
        "p95_minus_median": percentile(sorted(pooled), 0.95) - median_value,
        "late_2ms_pct": 100 * sum(v - median_value > 2 for v in pooled) / n,
        "late_5ms_pct": 100 * sum(v - median_value > 5 for v in pooled) / n,
        "late_10ms_pct": 100 * sum(v - median_value > 10 for v in pooled) / n,
        "consecutive_delta": statistics.mean(deltas) if deltas else 0.0,
    }
    for low, high in JITTER_BUCKETS:
        name = f"bucket_{low}_{'up' if high == float('inf') else high}ms_pct"
        metrics[name] = 100 * sum(low <= v < high for v in pooled) / n
    return metrics


def parse_header(file_path: Path) -> dict[str, str]:
    """'key: value' lines before the first blank line."""
    header = {}
    for line in file_path.read_text(encoding="utf-8").splitlines():
        if not line.strip():
            break
        if ":" in line:
            key, value = line.split(":", 1)
            header[key.strip()] = value.strip()
    return header


def round_files(directory: Path) -> list[Path]:
    return sorted(p for p in directory.glob("round*.txt") if not p.stem.endswith("_analysis"))


def write_summary(output_path: Path, rounds: list[tuple[Path, list[float], int]],
                  header: list[str]) -> Path:
    """Pool several rounds into one summary: overall stats plus per-round spread.

    rounds: (round file, values, lost) per round.
    """
    pooled = sorted(v for _, values, _ in rounds for v in values)
    lost = sum(lost for _, _, lost in rounds)
    sent = len(pooled) + lost
    per_round = []
    for path, values, round_lost in rounds:
        s = sorted(values)
        per_round.append((path.name, statistics.median(s), percentile(s, 0.95),
                          percentile(s, 0.99), s[-1], round_lost))

    def spread(index: int) -> str:
        column = [r[index] for r in per_round]
        return f"{min(column):.6f} .. {max(column):.6f}"

    median_value = statistics.median(pooled)
    lines = header + [
        "",
        f"rounds: {len(rounds)}",
        f"samples: {len(pooled)}",
        f"lost: {lost}",
        f"loss_rate: {lost / sent:.6f}" if sent else "loss_rate: 0",
        f"min: {pooled[0]:.6f}",
        f"max: {pooled[-1]:.6f}",
        f"mean: {statistics.mean(pooled):.6f}",
        f"median: {median_value:.6f}",
        f"p95: {percentile(pooled, 0.95):.6f}",
        f"p99: {percentile(pooled, 0.99):.6f}",
        f"p99.9: {percentile(pooled, 0.999):.6f}",
        f"sigma_sample: {statistics.stdev(pooled) if len(pooled) > 1 else 0.0:.6f}",
        f"iqr: {percentile(pooled, 0.75) - percentile(pooled, 0.25):.6f}",
        f"mad: {mad(pooled, median_value):.6f}",
        "",
        "# jitter (late_*: % more than N ms over the median; bucket_*: % per range)",
    ] + [f"{k}: {v:.6f}" for k, v in jitter_metrics([values for _, values, _ in rounds]).items()] + [
        "",
        "# spread between rounds (min .. max)",
        f"round_median: {spread(1)}",
        f"round_p95: {spread(2)}",
        f"round_p99: {spread(3)}",
        f"round_max: {spread(4)}",
        "",
        "# per round: file median p95 p99 max lost",
    ] + [f"{name} {m:.3f} {p95:.3f} {p99:.3f} {mx:.3f} {lo}" for name, m, p95, p99, mx, lo in per_round]
    output_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return output_path


def summarize_directory(directory: Path) -> Path:
    """(Re)write each round's analysis and directory/summary.txt from its round*.txt files."""
    files = round_files(directory)
    if not files:
        raise FileNotFoundError(f"No round*.txt files in: {directory}")
    rounds = []
    for f in files:
        values = parse_values(f)
        write_analysis(f, values)
        rounds.append((f, values, int(parse_header(f).get("lost", 0))))
    first = parse_header(files[0])
    keep = ["distance", "obstacles", "indoor", "library", "port", "gap"]
    header = [f"{k}: {first[k]}" for k in keep if k in first]
    return write_summary(directory / "summary.txt", rounds, header)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Analyze benchmark files and generate *_analysis.txt summaries."
    )
    parser.add_argument("benchmark_file", type=Path, help="benchmark .txt file, or a directory of round*.txt files")
    args = parser.parse_args()

    benchmark_file = args.benchmark_file
    if benchmark_file.is_dir():
        output_path = summarize_directory(benchmark_file)
        print(f"Summary written to: {output_path}")
        return 0
    if not benchmark_file.exists():
        raise FileNotFoundError(f"Benchmark file not found: {benchmark_file}")

    values = parse_values(benchmark_file)
    if not values:
        raise ValueError(f"No numeric benchmark samples found in: {benchmark_file}")

    output_path = write_analysis(benchmark_file, values)
    print(f"Analysis written to: {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
