#!/usr/bin/env python3
"""Analyze benchmark text files and write sibling *_analysis.txt files."""

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


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Analyze benchmark files and generate *_analysis.txt summaries."
    )
    parser.add_argument("benchmark_file", type=Path, help="Path to benchmark .txt file")
    args = parser.parse_args()

    benchmark_file = args.benchmark_file
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
