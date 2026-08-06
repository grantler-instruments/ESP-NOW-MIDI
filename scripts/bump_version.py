#!/usr/bin/env python3
"""Bump library version in version.h, library.properties, idf_component.yml, and esp_now_midi.py."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]

VERSION_H = REPO_ROOT / "include" / "version.h"
LIBRARY_PROPERTIES = REPO_ROOT / "library.properties"
IDF_COMPONENT_YML = REPO_ROOT / "idf_component.yml"
ESP_NOW_MIDI_PY = REPO_ROOT / "esp_now_midi.py"
WIZARD_SYSEX = REPO_ROOT / "scripts" / "wizard" / "enomik_sysex.py"


def read_version() -> tuple[int, int, int]:
    text = VERSION_H.read_text(encoding="utf-8")
    major = int(re.search(r"ESP_NOW_MIDI_VERSION_MAJOR (\d+)", text).group(1))
    minor = int(re.search(r"ESP_NOW_MIDI_VERSION_MINOR (\d+)", text).group(1))
    patch = int(re.search(r"ESP_NOW_MIDI_VERSION_PATCH (\d+)", text).group(1))
    return major, minor, patch


def bump(major: int, minor: int, patch: int, part: str) -> tuple[int, int, int]:
    if part == "patch":
        return major, minor, patch + 1
    if part == "minor":
        return major, minor + 1, 0
    if part == "major":
        return major + 1, 0, 0
    raise ValueError(f"unknown part: {part}")


def update_version_h(major: int, minor: int, patch: int) -> None:
    text = VERSION_H.read_text(encoding="utf-8")
    text = re.sub(r"(#define ESP_NOW_MIDI_VERSION_MAJOR )\d+", rf"\g<1>{major}", text)
    text = re.sub(r"(#define ESP_NOW_MIDI_VERSION_MINOR )\d+", rf"\g<1>{minor}", text)
    text = re.sub(r"(#define ESP_NOW_MIDI_VERSION_PATCH )\d+", rf"\g<1>{patch}", text)
    VERSION_H.write_text(text, encoding="utf-8")


def update_library_properties(major: int, minor: int, patch: int) -> None:
    text = LIBRARY_PROPERTIES.read_text(encoding="utf-8")
    text = re.sub(
        r"^version=.*$",
        f"version={major}.{minor}.{patch}",
        text,
        flags=re.MULTILINE,
    )
    LIBRARY_PROPERTIES.write_text(text, encoding="utf-8")


def update_idf_component_yml(major: int, minor: int, patch: int) -> None:
    text = IDF_COMPONENT_YML.read_text(encoding="utf-8")
    text = re.sub(
        r'^version:\s*".*"$',
        f'version: "{major}.{minor}.{patch}"',
        text,
        count=1,
        flags=re.MULTILINE,
    )
    IDF_COMPONENT_YML.write_text(text, encoding="utf-8")


def update_esp_now_midi_py(major: int, minor: int, patch: int) -> None:
    text = ESP_NOW_MIDI_PY.read_text(encoding="utf-8")
    text = re.sub(r"(VERSION_MAJOR = )\d+", rf"\g<1>{major}", text)
    text = re.sub(r"(VERSION_MINOR = )\d+", rf"\g<1>{minor}", text)
    text = re.sub(r"(VERSION_PATCH = )\d+", rf"\g<1>{patch}", text)
    ESP_NOW_MIDI_PY.write_text(text, encoding="utf-8")


def update_wizard_sysex(major: int, minor: int) -> None:
    text = WIZARD_SYSEX.read_text(encoding="utf-8")
    text = re.sub(r"(PROTOCOL_MAJOR = )\d+", rf"\g<1>{major}", text)
    text = re.sub(r"(PROTOCOL_MINOR = )\d+", rf"\g<1>{minor}", text)
    WIZARD_SYSEX.write_text(text, encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description="Bump ESP-NOW-MIDI version")
    parser.add_argument(
        "part",
        nargs="?",
        default="patch",
        choices=["patch", "minor", "major"],
        help="version component to bump (default: patch)",
    )
    args = parser.parse_args()

    old = read_version()
    new = bump(*old, args.part)
    old_str = ".".join(map(str, old))
    new_str = ".".join(map(str, new))

    update_version_h(*new)
    update_library_properties(*new)
    update_idf_component_yml(*new)
    update_esp_now_midi_py(*new)
    if args.part in ("major", "minor"):
        update_wizard_sysex(new[0], new[1])


    print(f"Bumped {old_str} → {new_str}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
