#!/usr/bin/env python3
"""Verify every hardcoded library version matches include/version.h."""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
VERSION_H = REPO_ROOT / "include" / "version.h"


def read_version_h() -> str:
    text = VERSION_H.read_text(encoding="utf-8")
    parts = [
        re.search(rf"ESP_NOW_MIDI_VERSION_{name} (\d+)", text).group(1)
        for name in ("MAJOR", "MINOR", "PATCH")
    ]
    return ".".join(parts)


def search(path: str, pattern: str) -> str | None:
    text = (REPO_ROOT / path).read_text(encoding="utf-8")
    match = re.search(pattern, text, re.MULTILINE)
    return match.group(1) if match else None


def read_esp_now_midi_py() -> str | None:
    text = (REPO_ROOT / "esp_now_midi.py").read_text(encoding="utf-8")
    parts = [re.search(rf"^VERSION_{name} = (\d+)", text, re.MULTILINE) for name in ("MAJOR", "MINOR", "PATCH")]
    if not all(parts):
        return None
    return ".".join(p.group(1) for p in parts)


def main() -> int:
    expected = read_version_h()
    found = {
        "library.properties": search("library.properties", r"^version=(\S+)"),
        "library.json": json.loads((REPO_ROOT / "library.json").read_text(encoding="utf-8")).get("version"),
        "idf_component.yml": search("idf_component.yml", r'^version:\s*"([^"]+)"'),
        "esp_now_midi.py": read_esp_now_midi_py(),
    }

    mismatches = [f"  - {name}: {value} (version.h={expected})" for name, value in found.items() if value != expected]
    if mismatches:
        print("Library version out of sync with include/version.h:", file=sys.stderr)
        print("\n".join(mismatches), file=sys.stderr)
        print("Use scripts/bump_version.py to bump versions.", file=sys.stderr)
        return 1

    print(f"Versions OK ({expected})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
