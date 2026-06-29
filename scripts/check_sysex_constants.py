#!/usr/bin/env python3
"""Verify enomik SysEx constants match between C++ and Python."""

from __future__ import annotations

import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
VERSION_H = REPO_ROOT / "version.h"
ENOMIK_SYSEX_H = REPO_ROOT / "enomik_sysex.h"

REQUEST_COMMANDS = (
    "SET_PIN_CONFIG",
    "GET_PIN_CONFIG",
    "CLEAR_PIN_CONFIGS",
    "GET_ALL_PIN_CONFIGS",
    "DELETE_PIN_CONFIG",
    "GET_MAC",
    "ADD_PEER",
    "GET_PEERS",
    "RESET",
    "GET_VERSION",
)

ERROR_CODES = (
    "BAD_VERSION",
    "UNKNOWN_COMMAND",
    "DECODE_FAILED",
    "PIN_NOT_FOUND",
    "NOT_READY",
    "OPERATION_FAILED",
)

RESPONSE_OFFSET = 0x40


def read_version() -> tuple[int, int, int]:
    text = VERSION_H.read_text(encoding="utf-8")
    major = int(re.search(r"ESP_NOW_MIDI_VERSION_MAJOR (\d+)", text).group(1))
    minor = int(re.search(r"ESP_NOW_MIDI_VERSION_MINOR (\d+)", text).group(1))
    patch = int(re.search(r"ESP_NOW_MIDI_VERSION_PATCH (\d+)", text).group(1))
    return major, minor, patch


def parse_cpp_enum(text: str, enum_name: str) -> dict[str, int]:
    match = re.search(
        rf"enum class {enum_name}\s*(?::\s*\w+)?\s*\{{([^}}]+)\}}",
        text,
        re.DOTALL,
    )
    if not match:
        raise ValueError(f"enum class {enum_name} not found in enomik_sysex.h")

    body = re.sub(r"//[^\n]*", "", match.group(1))
    values: dict[str, int] = {}
    for item in re.finditer(r"(\w+)\s*=\s*0x([0-9A-Fa-f]+)", body):
        values[item.group(1)] = int(item.group(2), 16)
    return values


def parse_manufacturer_id(text: str) -> int:
    match = re.search(r"MANUFACTURER_ID\s*=\s*0x([0-9A-Fa-f]+)", text)
    if not match:
        raise ValueError("MANUFACTURER_ID not found in enomik_sysex.h")
    return int(match.group(1), 16)


def load_python_constants():
    import importlib.util

    sysex_path = REPO_ROOT / "scripts" / "wizard" / "enomik_sysex.py"
    spec = importlib.util.spec_from_file_location("enomik_sysex", sysex_path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def collect_mismatches() -> list[str]:
    mismatches: list[str] = []

    cpp_text = ENOMIK_SYSEX_H.read_text(encoding="utf-8")
    commands = parse_cpp_enum(cpp_text, "SysExCommand")
    errors = parse_cpp_enum(cpp_text, "SysExErrorCode")
    manufacturer_id = parse_manufacturer_id(cpp_text)
    major, minor, _patch = read_version()

    sx = load_python_constants()

    if sx.MANUFACTURER_ID != manufacturer_id:
        mismatches.append(
            f"MANUFACTURER_ID: python=0x{sx.MANUFACTURER_ID:02X} "
            f"cpp=0x{manufacturer_id:02X}"
        )

    if sx.PROTOCOL_MAJOR != major:
        mismatches.append(f"PROTOCOL_MAJOR: python={sx.PROTOCOL_MAJOR} version.h={major}")
    if sx.PROTOCOL_MINOR != minor:
        mismatches.append(f"PROTOCOL_MINOR: python={sx.PROTOCOL_MINOR} version.h={minor}")

    for name in REQUEST_COMMANDS:
        py_name = f"CMD_{name}"
        py_val = getattr(sx, py_name)
        cpp_val = commands.get(name)
        if cpp_val is None:
            mismatches.append(f"missing C++ command {name}")
            continue
        if py_val != cpp_val:
            mismatches.append(
                f"{name}: python=0x{py_val:02X} cpp=0x{cpp_val:02X}"
            )

        resp_name = f"{name}_RESPONSE"
        py_resp = getattr(sx, f"RESP_{name}")
        cpp_resp = commands.get(resp_name)
        expected = py_val + RESPONSE_OFFSET
        if py_resp != expected:
            mismatches.append(
                f"python RESP_{name}: 0x{py_resp:02X} != CMD + 0x40 (0x{expected:02X})"
            )
        if cpp_resp is None:
            mismatches.append(f"missing C++ response {resp_name}")
        elif cpp_resp != expected:
            mismatches.append(
                f"{resp_name}: cpp=0x{cpp_resp:02X} != request + 0x40 (0x{expected:02X})"
            )

    cpp_error_response = commands.get("ERROR_RESPONSE")
    if cpp_error_response != sx.RESP_ERROR:
        mismatches.append(
            f"ERROR_RESPONSE: python=0x{sx.RESP_ERROR:02X} "
            f"cpp=0x{(cpp_error_response or 0):02X}"
        )
    if sx.RESP_ERROR != 0x7F:
        mismatches.append(f"RESP_ERROR must be 0x7F, got 0x{sx.RESP_ERROR:02X}")

    for name in ERROR_CODES:
        py_val = getattr(sx, f"ERR_{name}")
        cpp_val = errors.get(name)
        if cpp_val is None:
            mismatches.append(f"missing C++ error code {name}")
        elif py_val != cpp_val:
            mismatches.append(
                f"{name}: python=0x{py_val:02X} cpp=0x{cpp_val:02X}"
            )

    return mismatches


def check_sysex_constants() -> None:
    mismatches = collect_mismatches()
    if mismatches:
        details = "\n".join(f"  - {line}" for line in mismatches)
        raise AssertionError(
            "enomik SysEx constants out of sync between "
            "enomik_sysex.h and scripts/wizard/enomik_sysex.py:\n"
            f"{details}"
        )


def main() -> int:
    try:
        check_sysex_constants()
    except (AssertionError, ValueError) as err:
        print(err, file=sys.stderr)
        return 1
    print("SysEx constants OK (enomik_sysex.h ↔ enomik_sysex.py ↔ version.h)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
