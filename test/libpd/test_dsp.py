#!/usr/bin/env python3
"""Render stock and float-DSP libpd in separate processes and compare samples.

Build first using test/libpd/CMakeLists.txt; see components/libpd/README.md.
No Python dependencies beyond the standard library are required.
"""
from __future__ import annotations

import argparse
from array import array
import ctypes as C
import math
import random
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
STOCK = FLOAT = None


class Patch:
    def __init__(self):
        self.objects, self.wires = [], []

    def obj(self, text):
        i = len(self.objects)
        self.objects.append(f"#X obj 20 {20+i*20} {text};")
        return i

    def wire(self, a, b, outlet=0, inlet=0):
        self.wires.append(f"#X connect {a} {outlet} {b} {inlet};")

    def control(self, name):
        r, s = self.obj(f"r {name}"), self.obj("sig~")
        self.wire(r, s)
        return s

    def save(self, path):
        path.write_text("\n".join(["#N canvas 0 0 1000 900 12;", *self.objects, *self.wires])+"\n")


def worker(library, scenario, output):
    # Process isolation prevents global Pd symbols in two shared libraries
    # from interposing on each other.
    lib = C.CDLL(str(library))
    lib.libpd_openfile.argtypes = [C.c_char_p, C.c_char_p]
    lib.libpd_openfile.restype = C.c_void_p
    lib.libpd_closefile.argtypes = [C.c_void_p]
    lib.libpd_float.argtypes = [C.c_char_p, C.c_float]
    lib.libpd_add_float.argtypes = [C.c_float]
    lib.libpd_finish_message.argtypes = [C.c_char_p, C.c_char_p]
    lib.libpd_process_float.argtypes = [C.c_int, C.POINTER(C.c_float), C.POINTER(C.c_float)]
    lib.libpd_write_array.argtypes = [C.c_char_p, C.c_int, C.POINTER(C.c_float), C.c_int]
    errors = []
    @C.CFUNCTYPE(None, C.c_char_p)
    def printer(text):
        line = text.decode(errors="replace")
        if "error" in line or "couldn't" in line:
            errors.append(line)
    lib.libpd_set_printhook.argtypes = [type(printer)]
    lib.libpd_set_printhook(printer)
    assert lib.libpd_init() == 0
    assert lib.libpd_init_audio(0, 2, 48000) == 0

    def message(receiver, selector, values):
        assert lib.libpd_start_message(len(values)) == 0
        for value in values:
            lib.libpd_add_float(value)
        assert lib.libpd_finish_message(receiver.encode(), selector.encode()) == 0

    p = Patch()
    events = {}
    blocks = 32
    table = None
    compatibility = scenario.endswith(":old")
    scenario = scenario.removesuffix(":old")
    if compatibility:
        message("pd", "compatibility", [0.54])
    if scenario.startswith("math:"):
        name = scenario.split(":")[1]
        a = p.control("a")
        binary = name.startswith(("log", "pow"))
        scalar = name.endswith("scalar")
        reverse = name.endswith("reverse")
        name = name.removesuffix("scalar").removesuffix("reverse")
        single = name.endswith("single")
        name = name.removesuffix("single")
        op = p.obj("mathsingle" if single else name + (" 2" if scalar else ""))
        if reverse:
            p.wire(p.obj("r a"), op)
        else:
            p.wire(a, op)
        if binary and not scalar:
            p.wire(p.control("b"), op, inlet=1)
        values = [-2, -0.001, 0, 0.0001, 0.125, 0.5, 1, 2, 10, 60, 100, 120]
        blocks = len(values)
        if name == "mtof~":
            values += [-1501, -1500, 1499, 1500]
            blocks = len(values)
        for i, value in enumerate(values):
            events[i] = [("a", "float", [value]), ("b", "float", [2 if i % 2 else 0.5])]
    elif scenario.startswith(("phasor", "osc", "cos", "tabosc4", "vcf")):
        name = scenario.split(":")[0]
        op = p.obj(name + (" table" if name == "tabosc4~" else " 4" if name == "vcf~" else ""))
        p.wire(p.control("a"), op)
        if name == "vcf~":
            p.wire(p.control("freq"), op, inlet=1)
        elif name in ("phasor~", "osc~", "tabosc4~"):
            p.wire(p.obj("r phase"), op, inlet=1)
        blocks = 1500 if "long" in scenario or "negative" in scenario else 96
        frequency = -440 if "negative" in scenario else 0.001 if "slow" in scenario else 100000 if "high" in scenario else 440
        events[0] = [("a", "float", [frequency]), ("freq", "float", [1500])]
        if name == "cos~":
            events = {i: [("a", "float", [v])] for i, v in enumerate([-100.125, -1, -.25, 0, .125, .5, .999, 1, 100.125])}
            blocks = len(events)
        if "reset" in scenario:
            for i, value in [(2, -.25), (8, 2.75), (16, .125), (32, 0)]:
                events[i] = [("phase", "float", [value])]
        if name == "tabosc4~":
            p.obj("array define table 1027")
            table = array("f", (math.cos((i-1)*math.tau/1024) for i in range(1027)))
    elif scenario.startswith("tabread4"):
        p.obj("array define table 1048580")
        op = p.obj("tabread4~ table")
        p.wire(p.control("a"), op)
        p.wire(p.control("onset"), op, inlet=1)
        table = array("f", ((i % 97)/97 for i in range(1048580)))
        cases = [(i, 0) for i in [-2, 0, 1, 1.125, 2.875, 100.01]]
        cases += [(v, 1048576) for v in [-.999, -.125, .001, .125, .875, 1.5, 4]]
        cases += [(1048576, -1048574.875), (-1048576, 1048577.125), (1e20, 0), (-1e20, 0)]
        events = {i: [("a", "float", [a]), ("onset", "float", [o])] for i, (a, o) in enumerate(cases)}
        blocks = len(cases)
    elif scenario.startswith("vline"):
        op = p.obj("ramp128" if scenario == "vline:reblock" else "vline~")
        if scenario != "vline:reblock":
            p.wire(p.obj("r ramp"), op)
        events[0] = [("ramp", "list", v) for v in [[0, 0, 0], [1, 10.25, .375], [.1, 30, 15], [.5, 0, 50]]]
        events[4] = [("ramp", "list", [0.8, 2.5, .125])]
        events[7] = [("ramp", "stop", [])]
        events[12] = [("ramp", "list", v) for v in [[-1, 0, 0], [1, 0, .020833333], [0, 0, .041666667], [.25, 1, .5]]]
        events[50] = [("ramp", "list", [1, 100, 0])]
        blocks = 150
        if scenario == "vline:long":
            events = {0: [("ramp", "list", [1, 3000, 0])]}
            blocks = 2400
    else:
        raise ValueError(scenario)
        if scenario == "vline:stress":
            rng = random.Random(20261120)
            events = {}
            blocks = 300
            for i in range(0, blocks, 3):
                events[i] = [("ramp", "list", [rng.uniform(-1, 1), rng.choice([0, .020833333, .125, 1, 10]), rng.choice([0, .020833333, .375, 2])]) for _ in range(3)]
    dac = p.obj("dac~")
    p.wire(op, dac)
    p.wire(op, dac, outlet=1 if scenario.startswith("vcf") else 0, inlet=1)
    with tempfile.TemporaryDirectory() as tmp:
        patch = Path(tmp)/"test.pd"
        p.save(patch)
        if scenario.endswith("single"):
            sub = Patch()
            sub.obj("block~ 1")
            a, b, o = sub.obj("inlet~"), sub.obj(scenario.split(":")[1].removesuffix("single")), sub.obj("outlet~")
            sub.wire(a, b)
            sub.wire(b, o)
            sub.save(Path(tmp)/"mathsingle.pd")
        if scenario == "vline:reblock":
            sub = Patch()
            sub.obj("block~ 128")
            r, v, o = sub.obj("r ramp"), sub.obj("vline~"), sub.obj("outlet~")
            sub.wire(r, v)
            sub.wire(v, o)
            sub.save(Path(tmp)/"ramp128.pd")
        handle = lib.libpd_openfile(patch.name.encode(), str(patch.parent).encode())
        assert handle, "patch failed to open"
        if table is not None:
            data = (C.c_float * len(table)).from_buffer(table)
            assert lib.libpd_write_array(b"table", 0, data, len(table)) == 0
        message("pd", "dsp", [1])
        audio, empty = (C.c_float*128)(), (C.c_float*1)()
        result = array("f")
        if scenario == "vline:late":
            # Advance ten minutes without retaining the warm-up output.
            discard = (C.c_float*(128*4096))()
            for _ in range(110):
                assert lib.libpd_process_float(4096, empty, discard) == 0
        for block in range(blocks):
            for receiver, selector, values in events.get(block, []):
                if selector == "float":
                    lib.libpd_float(receiver.encode(), values[0])
                else:
                    message(receiver, selector, values)
            assert lib.libpd_process_float(1, empty, audio) == 0
            result.extend(audio)
        lib.libpd_closefile(handle)
        assert not errors, errors
        with open(output, "wb") as stream:
            result.tofile(stream)


class DspComparison(unittest.TestCase):
    def render(self, library, scenario):
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp)/"samples.f32"
            result = subprocess.run([sys.executable, __file__, "--worker", str(library), scenario, str(output)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
            samples = array("f")
            samples.frombytes(output.read_bytes())
            return samples

    def compare(self, scenario, atol=2e-6, rtol=2e-5, circular=False, exact=False):
        stock, fast = self.render(STOCK, scenario), self.render(FLOAT, scenario)
        self.assertEqual(len(stock), len(fast))
        worst = 0
        for i, (a, b) in enumerate(zip(stock, fast)):
            if not math.isfinite(a):
                self.assertTrue((math.isnan(a) and math.isnan(b)) or a == b, (scenario, i, a, b))
                continue
            self.assertTrue(math.isfinite(b), (scenario, i, a, b))
            error = abs(a-b)
            if circular:
                error = min(error, abs(1-error))
            worst = max(worst, error)
            self.assertLessEqual(error, 0 if exact else atol+rtol*abs(a), (scenario, i, a, b, error))
        print(f"  {scenario}: max absolute error {worst:.8g}", flush=True)

    def test_math(self):
        for obj in ["mtof~", "ftom~", "dbtorms~", "rmstodb~", "dbtopow~", "powtodb~", "sqrt~", "rsqrt~", "sqrt~single", "rsqrt~single", "exp~", "log~", "pow~", "log~scalar", "pow~scalar", "log~reverse", "pow~reverse"]:
            with self.subTest(obj=obj):
                self.compare("math:"+obj)

    def test_oscillators(self):
        for obj in ["phasor~", "osc~", "tabosc4~"]:
            for case in ["long", "negative", "slow", "reset", "high"]:
                with self.subTest(obj=obj, case=case):
                    self.compare(obj+":"+case, atol=5e-5, circular=obj == "phasor~")
        self.compare("cos~")
        self.compare("vcf~", atol=2e-5)

    def test_old_compatibility(self):
        for obj in ["osc~", "cos~", "vcf~"]:
            with self.subTest(obj=obj):
                self.compare(obj+":old", atol=2e-6)

    def test_table_index_precision(self):
        self.compare("tabread4~", atol=2e-7, rtol=2e-6)

    def test_vline(self):
        for case in ["vline", "vline:long", "vline:reblock", "vline:stress", "vline:late"]:
            with self.subTest(case=case):
                self.compare(case, atol=2e-6, rtol=2e-6)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stock", type=Path)
    parser.add_argument("--float", dest="float_lib", type=Path)
    parser.add_argument("--worker", nargs=3, metavar=("LIBRARY", "SCENARIO", "OUTPUT"))
    args = parser.parse_args()
    if args.worker:
        worker(*args.worker)
    else:
        if not args.stock or not args.float_lib:
            parser.error("--stock and --float shared library paths are required")
        STOCK, FLOAT = args.stock.resolve(), args.float_lib.resolve()
        unittest.main(argv=[sys.argv[0]], verbosity=2)
