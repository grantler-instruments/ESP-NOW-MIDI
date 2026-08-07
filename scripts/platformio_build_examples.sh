#!/usr/bin/env bash
# Build all PlatformIO example projects that have a platformio.ini.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
failed_names=()

for example_dir in "$ROOT"/examples/*/; do
  [[ -d "$example_dir" ]] || continue
  name="$(basename "$example_dir")"
  ini="$example_dir/platformio.ini"

  if [[ ! -f "$ini" ]]; then
    echo "Skipping $name (no platformio.ini)"
    continue
  fi

  echo "========================================"
  echo "Building $name"
  echo "========================================"
  if (cd "$example_dir" && pio run); then
    echo "OK: $name"
  else
    echo "FAILED: $name"
    failed_names+=("$name")
  fi
done

if [[ "${#failed_names[@]}" -ne 0 ]]; then
  echo "One or more PlatformIO example builds failed:"
  for name in "${failed_names[@]}"; do
    echo "  - $name"
  done
  exit 1
fi

echo "All PlatformIO examples built successfully."
