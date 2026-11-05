#!/usr/bin/env bash
# Run a round-trip benchmark series (scripts/benchmark_series.py) in the
# wizard's virtualenv (scripts/wizard/.venv), creating it on first use.
# All arguments go to benchmark_series.py, e.g.:
#   scripts/benchmark.sh                  # default series
#   scripts/benchmark.sh 1:0 3:0 5:1      # distance:obstacles
#   scripts/benchmark.sh --help

set -euo pipefail

SCRIPTS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VENV="$SCRIPTS/wizard/.venv"
PY="$VENV/bin/python"

if [[ ! -x "$PY" ]]; then
  echo "Creating venv at scripts/wizard/.venv ..."
  python3 -m venv "$VENV"
fi
if ! "$PY" -c "import mido, rtmidi" >/dev/null 2>&1; then
  echo "Installing mido and python-rtmidi into the venv ..."
  "$VENV/bin/pip" install --disable-pip-version-check -r "$SCRIPTS/wizard/requirements.txt"
fi

export PYTHONUNBUFFERED=1
exec "$PY" "$SCRIPTS/benchmark_series.py" "$@"
