#!/usr/bin/env python3
"""Verify each selected DSP function's linked section, including static variants.

Usage: python3 test/libpd/audit_iram.py /path/to/firmware.elf \
    --compile-commands /path/to/build/compile_commands.json
Use --expect flash for the corresponding baseline build.
"""
import argparse
import json
from pathlib import Path
import re
import shlex
import subprocess

DEFAULT_FRAGMENT = Path(__file__).resolve().parents[2] / "components/libpd/libpd_hot.lf"


def audit(elf, commands, fragment=DEFAULT_FRAGMENT, expect="iram", allow_unused=False):
    entries = re.findall(r"^\s+([\w.]+):([\w]+)\s+\(noflash\)\s*$", fragment.read_text(), re.M)
    if not entries:
        raise RuntimeError("No function mappings found")
    database = json.loads(commands.read_text())
    command = next(item for item in database if Path(item["file"]).name == "d_ugen.c")
    args = command.get("arguments") or shlex.split(command["command"])
    compiler = Path(args[0])
    objdump = compiler.with_name(compiler.name.removesuffix("gcc") + "objdump")
    output = subprocess.check_output([str(objdump), "-t", str(elf)], text=True)
    symbols = {}
    for line in output.splitlines():
        fields = line.split()
        if len(fields) == 6 and fields[2] == "F":
            symbols[fields[5]] = (fields[3], int(fields[4], 16))
    errors, size, unused = [], 0, 0
    for obj, name in entries:
        if name not in symbols:
            if not allow_unused:
                errors.append(f"{obj}:{name}: missing from ELF")
            unused += 1
            continue
        section, length = symbols[name]
        valid = section.startswith(".iram") if expect == "iram" else section == ".flash.text"
        if not valid:
            errors.append(f"{obj}:{name}: found in {section}, expected {expect}")
        size += length
    # The placement list deliberately leaves old compatibility code in flash.
    for name in ["cos_perform_old", "osc_perform_old", "sigvcf_perform_old", "sighip_perform_old"]:
        if name in symbols and symbols[name][0] != ".flash.text":
            errors.append(f"Legacy routine {name} unexpectedly moved out of flash")
    # Validate emitted literal pools as well as function symbols when a map
    # accompanies the ELF. Discarded input sections have address zero.
    map_path = elf.with_suffix(".map")
    literal_count = 0
    if map_path.exists():
        mapping = map_path.read_text()
        section_name = ".iram0.text" if expect == "iram" else ".flash.text"
        bounds = re.search(r"^" + re.escape(section_name) + r"\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)", mapping, re.M)
        if not bounds:
            errors.append(f"No {section_name} output section found in map")
        else:
            low = int(bounds[1], 16)
            high = low + int(bounds[2], 16)
            for _, name in entries:
                pools = re.findall(r"^ \.literal\." + re.escape(name) + r"\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)", mapping, re.M)
                for address, length in pools:
                    address, length = int(address, 16), int(length, 16)
                    if address and length:
                        literal_count += 1
                        if not (low <= address and address+length <= high):
                            errors.append(f"Literal pool {name} is outside {section_name}")
    if errors:
        raise RuntimeError("\n".join(errors))
    print(f"Verified {len(entries)-unused} selected routines in {expect}; {size} function bytes; {unused} unused routines removed.")
    if map_path.exists():
        print(f"Verified {literal_count} nonempty literal pools in {expect}.")
    print("Literal pools and alignment must be included separately using the linker map.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path)
    parser.add_argument("--compile-commands", required=True, type=Path)
    parser.add_argument("--fragment", type=Path, default=DEFAULT_FRAGMENT)
    parser.add_argument("--expect", choices=["iram", "flash"], default="iram")
    parser.add_argument("--allow-unused", action="store_true", help="Allow selected functions removed by linker garbage collection in minimal apps")
    options = parser.parse_args()
    audit(options.elf, options.compile_commands, options.fragment, options.expect, options.allow_unused)
