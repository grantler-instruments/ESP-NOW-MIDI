#!/usr/bin/env bash
# Build all PlatformIO example projects that have a platformio.ini.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
failed_names=()

# Recursive, so nested examples (e.g. examples/i2c/*) are built too. Skip
# PlatformIO's own .pio folders, which can contain dependency projects.
while IFS= read -r -d '' ini; do
  example_dir="$(dirname "$ini")"
  name="${example_dir#"$ROOT"/examples/}"

  echo "========================================"
  echo "Building $name"
  echo "========================================"
  if (cd "$example_dir" && pio run); then
    echo "OK: $name"
  else
    echo "FAILED: $name"
    failed_names+=("$name")
  fi
done < <(find "$ROOT/examples" -name .pio -prune -o -type f -name platformio.ini -print0 | sort -z)

if [[ "${#failed_names[@]}" -ne 0 ]]; then
  echo "One or more PlatformIO example builds failed:"
  for name in "${failed_names[@]}"; do
    echo "  - $name"
  done
  exit 1
fi

echo "All PlatformIO examples built successfully."
