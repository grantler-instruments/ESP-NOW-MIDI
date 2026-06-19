#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

echo "Running CircuitPython unit tests..."
python3 -m unittest discover -s test/circuitpython -v

if command -v mpy-cross >/dev/null 2>&1; then
  echo "Compiling esp_now_midi.py with mpy-cross..."
  mpy-cross esp_now_midi.py -o /tmp/esp_now_midi.mpy
  echo "mpy-cross compile succeeded."
else
  echo "mpy-cross not found; skipping bytecode compile (CI will run this step)."
fi
