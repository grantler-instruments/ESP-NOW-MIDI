#!/usr/bin/env python3
"""Check ESP32 object code for double operations in the targeted stateless loops.

Usage: python3 test/libpd/audit_target.py /path/to/build/compile_commands.json
Oscillator state boundaries and vline scheduling intentionally retain doubles;
they require inspection rather than a blanket no-double assertion.
"""
import argparse
import json
from pathlib import Path
import re
import shlex
import subprocess

EXPECTED = {
    "d_math.c": {
        "sigsqrt_perform", "sigsqrt_perform8", "sigrsqrt_perform",
        "sigrsqrt_perform8", "mtof_tilde_perform", "ftom_tilde_perform",
        "dbtorms_tilde_perform", "rmstodb_tilde_perform",
        "dbtopow_tilde_perform", "powtodb_tilde_perform", "exp_tilde_perform",
    },
    "d_arithmetic.c": {
        "log_tilde_perform", "log_tilde_perform_scalar",
        "log_tilde_perform_reversescalar", "pow_tilde_perform",
        "pow_tilde_perform_scalar", "pow_tilde_perform_reversescalar",
    },
    "d_array.c": {"tabread4_tilde_perform"},
    "d_osc.c": {"cos_perform", "sigvcf_perform"},
}
DOUBLE = re.compile(r"^(?:__\w*df\w*|exp|log|pow|sqrt|floor|ceil|fmod)$")


def audit(database):
    checked, failures = set(), []
    for entry in json.loads(database.read_text()):
        source = Path(entry["file"]).name
        if source not in EXPECTED:
            continue
        args = entry.get("arguments") or shlex.split(entry["command"])
        if "-DPD_FLOAT_DSP=1" not in args:
            raise RuntimeError(f"{source}: PD_FLOAT_DSP is not enabled")
        obj = Path(entry["directory"]) / args[args.index("-o")+1]
        objdump = Path(args[0]).with_name(Path(args[0]).name.removesuffix("gcc")+"objdump")
        asm = subprocess.check_output([str(objdump), "-dr", str(obj)], text=True)
        sections = re.findall(r"Disassembly of section ([^:]+):\n(.*?)(?=Disassembly of section |\Z)", asm, re.S)
        for section, body in sections:
            if not section.startswith(".text."):
                continue
            function = section.removeprefix(".text.")
            if function not in EXPECTED[source]:
                continue
            checked.add((source, function))
            refs = re.findall(r"R_XTENSA_\w+\s+([\w]+)", body)
            bad = sorted(set(filter(DOUBLE.match, refs)))
            if bad:
                failures.append(f"{source}:{function}: {', '.join(bad)}")
    missing = {(source, function) for source, functions in EXPECTED.items() for function in functions} - checked
    if missing:
        failures.append("Missing routines: " + ", ".join(f"{s}:{f}" for s, f in sorted(missing)))
    if failures:
        raise RuntimeError("\n".join(failures))
    print(f"Checked {len(checked)} current DSP routines: no software double or double maths references.")
    print("Oscillator state boundaries, vline scheduling, setup and legacy routines are excluded.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("compile_commands", type=Path)
    audit(parser.parse_args().compile_commands)
