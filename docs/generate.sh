#!/usr/bin/env bash
# Generate the documentation site: Doxygen XML -> doxybook2 markdown -> MkDocs.
#
# Usage (from anywhere, e.g. the docs/ directory):
#   ./generate.sh          # build the site into <repo>/site
#   ./generate.sh serve    # build and serve locally with live reload
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TOOLS_DIR="$ROOT/.tools"
DOXYBOOK2_VERSION="v1.5.0"

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

if [ -x "$ROOT/.venv/bin/mkdocs" ]; then
  MKDOCS="$ROOT/.venv/bin/mkdocs"
elif command -v mkdocs >/dev/null 2>&1; then
  MKDOCS="mkdocs"
else
  echo "error: mkdocs not found. Set it up with:" >&2
  echo "  python3 -m venv $ROOT/.venv && $ROOT/.venv/bin/pip install -r $ROOT/docs/requirements.txt" >&2
  exit 1
fi

# --- Generate ----------------------------------------------------------------

cd "$ROOT"

# Keep docs version in sync with include/version.h (single source of truth).
VERSION_MAJOR="$(sed -nE 's/.*ESP_NOW_MIDI_VERSION_MAJOR[[:space:]]+([0-9]+).*/\1/p' include/version.h)"
VERSION_MINOR="$(sed -nE 's/.*ESP_NOW_MIDI_VERSION_MINOR[[:space:]]+([0-9]+).*/\1/p' include/version.h)"
VERSION_PATCH="$(sed -nE 's/.*ESP_NOW_MIDI_VERSION_PATCH[[:space:]]+([0-9]+).*/\1/p' include/version.h)"
VERSION="${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}"
if [[ ! "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "error: could not parse version from include/version.h" >&2
  exit 1
fi

echo "==> Running doxygen (PROJECT_NUMBER=$VERSION)"
# Override PROJECT_NUMBER without editing Doxyfile.
(cat Doxyfile; printf 'PROJECT_NUMBER = %s\n' "$VERSION") | doxygen -

echo "==> Generating API markdown with doxybook2"
rm -rf docs/api
mkdir -p docs/api
"$DOXYBOOK2" --input .doxygen/xml --output docs/api --config docs/doxybook.json

if [ "${1:-build}" = "serve" ]; then
  echo "==> Serving site at http://127.0.0.1:8000/"
  exec "$MKDOCS" serve --config-file mkdocs.yml
else
  echo "==> Building site into $ROOT/site"
  "$MKDOCS" build --strict --config-file mkdocs.yml
fi
