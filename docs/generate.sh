#!/usr/bin/env bash
# Generate the documentation site: Doxygen XML -> doxybook2 markdown -> MkDocs.
#
# Usage (from anywhere, e.g. the docs/ directory):
#   ./generate.sh           # build the site into <repo>/site
#   ./generate.sh serve     # build and serve locally with live reload
#   ./generate.sh prepare   # doxygen + doxybook2 only (for mike / CI)
#
# Env:
#   DOCS_ROOT     Source tree to document (default: repo containing this script)
#   DOCS_VERSION  Version id for absolute API links (e.g. latest, 0.18).
#                 When set, doxybook baseUrl is /ESP-NOW-MIDI/$DOCS_VERSION/api/
#                 When unset, baseUrl is /api/ (local serve).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${DOCS_ROOT:-$SCRIPT_DIR/..}" && pwd)"
TOOLS_DIR="${DOCS_TOOLS_DIR:-$(cd "$SCRIPT_DIR/.." && pwd)/.tools}"
DOXYBOOK2_VERSION="v1.5.0"
MODE="${1:-build}"

# --- Resolve tools -----------------------------------------------------------

if ! command -v doxygen >/dev/null 2>&1; then
  echo "error: doxygen not found. Install it with: brew install doxygen" >&2
  exit 1
fi

if command -v doxybook2 >/dev/null 2>&1; then
  DOXYBOOK2="doxybook2"
elif [ -x "$TOOLS_DIR/doxybook2/bin/doxybook2" ]; then
  DOXYBOOK2="$TOOLS_DIR/doxybook2/bin/doxybook2"
else
  echo "doxybook2 not found, downloading $DOXYBOOK2_VERSION to $TOOLS_DIR ..."
  case "$(uname -s)" in
    Darwin) DOXYBOOK2_ASSET="doxybook2-osx-amd64-$DOXYBOOK2_VERSION.zip" ;;
    Linux)  DOXYBOOK2_ASSET="doxybook2-linux-amd64-$DOXYBOOK2_VERSION.zip" ;;
    *) echo "error: unsupported OS, install doxybook2 manually" >&2; exit 1 ;;
  esac
  mkdir -p "$TOOLS_DIR/doxybook2"
  curl -fsSL -o "$TOOLS_DIR/doxybook2.zip" \
    "https://github.com/matusnovak/doxybook2/releases/download/$DOXYBOOK2_VERSION/$DOXYBOOK2_ASSET"
  unzip -qo "$TOOLS_DIR/doxybook2.zip" -d "$TOOLS_DIR/doxybook2"
  rm "$TOOLS_DIR/doxybook2.zip"
  chmod +x "$TOOLS_DIR/doxybook2/bin/doxybook2"
  DOXYBOOK2="$TOOLS_DIR/doxybook2/bin/doxybook2"
fi

if [ "$MODE" != "prepare" ]; then
  if [ -x "$(cd "$SCRIPT_DIR/.." && pwd)/.venv/bin/mkdocs" ]; then
    MKDOCS="$(cd "$SCRIPT_DIR/.." && pwd)/.venv/bin/mkdocs"
  elif command -v mkdocs >/dev/null 2>&1; then
    MKDOCS="mkdocs"
  else
    echo "error: mkdocs not found. Set it up with:" >&2
    echo "  python3 -m venv $(cd "$SCRIPT_DIR/.." && pwd)/.venv && $(cd "$SCRIPT_DIR/.." && pwd)/.venv/bin/pip install -r $SCRIPT_DIR/requirements.txt" >&2
    exit 1
  fi
fi

# --- Generate ----------------------------------------------------------------

cd "$ROOT"

if [ ! -f include/version.h ]; then
  echo "error: include/version.h not found in $ROOT" >&2
  exit 1
fi

# Keep docs version in sync with include/version.h (single source of truth).
VERSION_MAJOR="$(sed -nE 's/.*ESP_NOW_MIDI_VERSION_MAJOR[[:space:]]+([0-9]+).*/\1/p' include/version.h)"
VERSION_MINOR="$(sed -nE 's/.*ESP_NOW_MIDI_VERSION_MINOR[[:space:]]+([0-9]+).*/\1/p' include/version.h)"
VERSION_PATCH="$(sed -nE 's/.*ESP_NOW_MIDI_VERSION_PATCH[[:space:]]+([0-9]+).*/\1/p' include/version.h)"
VERSION="${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}"
if [[ ! "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "error: could not parse version from include/version.h" >&2
  exit 1
fi

if [ -n "${DOCS_VERSION:-}" ]; then
  DOXYBOOK_BASE_URL="/ESP-NOW-MIDI/${DOCS_VERSION}/api/"
else
  DOXYBOOK_BASE_URL="/api/"
fi

DOXYBOOK_TEMPLATE="$ROOT/docs/doxybook.json"
if [ ! -f "$DOXYBOOK_TEMPLATE" ]; then
  DOXYBOOK_TEMPLATE="$SCRIPT_DIR/doxybook.json"
fi
DOXYBOOK_CONFIG="$(mktemp)"
python3 - "$DOXYBOOK_TEMPLATE" "$DOXYBOOK_CONFIG" "$DOXYBOOK_BASE_URL" <<'PY'
import json, sys
src, dst, base_url = sys.argv[1], sys.argv[2], sys.argv[3]
with open(src, encoding="utf-8") as f:
    cfg = json.load(f)
cfg["baseUrl"] = base_url
with open(dst, "w", encoding="utf-8") as f:
    json.dump(cfg, f, indent=2)
    f.write("\n")
PY
trap 'rm -f "$DOXYBOOK_CONFIG"' EXIT

echo "==> Running doxygen (PROJECT_NUMBER=$VERSION, doxybook baseUrl=$DOXYBOOK_BASE_URL)"
# Override PROJECT_NUMBER without editing Doxyfile.
(cat Doxyfile; printf 'PROJECT_NUMBER = %s\n' "$VERSION") | doxygen -

echo "==> Generating API markdown with doxybook2"
rm -rf docs/api
mkdir -p docs/api
"$DOXYBOOK2" --input .doxygen/xml --output docs/api --config "$DOXYBOOK_CONFIG"

if [ "$MODE" = "prepare" ]; then
  echo "==> Prepare complete (skipped MkDocs build)"
  exit 0
fi

if [ "$MODE" = "serve" ]; then
  echo "==> Serving site at http://127.0.0.1:8000/"
  exec "$MKDOCS" serve --config-file mkdocs.yml
fi

echo "==> Building site into $ROOT/site"
"$MKDOCS" build --strict --config-file mkdocs.yml
