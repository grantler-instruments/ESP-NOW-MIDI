#!/usr/bin/env bash
# Build all PlatformIO example projects that have a platformio.ini.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
failed=0

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
    failed=1
  fi
done

if [[ "$failed" -ne 0 ]]; then
  echo "One or more PlatformIO example builds failed."
  exit 1
fi

echo "All PlatformIO examples built successfully."
