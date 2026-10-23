#!/usr/bin/env bash
# Run the board tester (or `run.sh client_test ...`: the client_test wizard)
# in a project-local virtualenv (.venv/).
# Nothing is installed into system Python or user site-packages.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

VENV="$ROOT/.venv"
PY="$VENV/bin/python"
PIP="$VENV/bin/pip"

SCRIPT=wizard.py
if [[ "${1:-}" == "client_test" ]]; then
  SCRIPT=client_test.py
  shift
fi

echo "ESP-NOW MIDI ${SCRIPT%.py}"

if [[ ! -x "$PY" ]]; then
  echo "Creating isolated venv at scripts/wizard/.venv ..."
  python3 -m venv "$VENV"
fi

if ! "$PY" -c "import mido" >/dev/null 2>&1; then
  echo "Installing Python dependencies into .venv (first run only) ..."
  "$PIP" install --disable-pip-version-check -r requirements.txt
else
  echo "Using existing .venv dependencies."
fi

export PYTHONUNBUFFERED=1
exec "$PY" "$SCRIPT" "$@"
