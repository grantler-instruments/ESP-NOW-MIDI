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
    p95_value = percentile(sorted_values, 0.95)
    p99_value = percentile(sorted_values, 0.99)

    lines = [
        f"source_file: {file_path.name}",
        f"samples: {len(values)}",
        f"min: {min_value:.6f}",
        f"max: {max_value:.6f}",
        f"mean: {mean_value:.6f}",
        f"median: {median_value:.6f}",
        f"p95: {p95_value:.6f}",
        f"p99: {p99_value:.6f}",
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
