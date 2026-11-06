"""Pd abstractions in pd-patches/enomik build and parse the same SysEx as the wizard.

Runs Pure Data headless. Set PD to the pd binary if it is not on PATH or in
/Applications; the tests skip when Pd is missing. The MIDI port test also needs
mido + python-rtmidi with virtual port support (macOS, Linux ALSA).
"""

from __future__ import annotations

import glob
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
ENOMIK_DIR = REPO_ROOT / "pd-patches" / "enomik"
sys.path.insert(0, str(REPO_ROOT / "scripts" / "wizard"))

import enomik_sysex as e  # noqa: E402

MAC = [0x84, 0xF7, 0x03, 0xF2, 0x54, 0x62]
MAC_NIBBLES = [8, 4, 15, 7, 0, 3, 15, 2, 5, 4, 6, 2]
HEADER = [0xF0, e.MANUFACTURER_ID, e.PROTOCOL_MAJOR, e.PROTOCOL_MINOR]


def find_pd() -> str | None:
    candidates = [os.environ.get("PD"), shutil.which("pd")]
    candidates += sorted(glob.glob("/Applications/Pd-*.app/Contents/Resources/bin/pd"), reverse=True)
    return next((c for c in candidates if c and os.path.exists(c)), None)


PD = find_pd()


def raw(cmd: int, *payload: int) -> list[int]:
    return e.build_raw(cmd, list(payload))


def reply(cmd: int, *payload: int) -> str:
    return " ".join(str(b) for b in HEADER + [cmd, *payload, 0xF7])


# (abstraction, Pd message, expected inner SysEx data)
REQUESTS = [
    ("get_version", "bang", e.build_get_version()),
    ("get_config", "bang", e.build_get_config()),
    ("get_mac", "bang", e.build_get_mac()),
    ("set_pin_config", "3 2 0 1 144 60 0 127", e.build_set_pin_config(3, 2, 1, 144, 60, 0, 127, 0)),
    ("set_pin_config", "5 5 30 2 88 7 10 100", e.build_set_pin_config(5, 5, 2, 176, 7, 10, 100, 30)),
    ("get_pin_config", "7", e.build_get_pin_config(7)),
    ("delete_pin_config", "7", raw(e.CMD_DELETE_PIN_CONFIG, 7)),
    ("get_all_pin_configs", "bang", e.build_get_all_pin_configs()),
    ("clear_pin_configs", "bang", raw(e.CMD_CLEAR_PIN_CONFIGS)),
    ("add_peer", "symbol 84:F7:03:F2:54:62", e.build_add_peer(MAC)),
    ("add_peer", "132 247 3 242 84 98", e.build_add_peer(MAC)),
    ("add_peer", "symbol 84-f7-03-f2-54-62", e.build_add_peer(MAC)),
    ("get_peer", "2", e.build_get_peer(2)),
    ("get_all_peers", "bang", e.build_get_all_peers()),
    ("set_name", "pad left", e.build_set_name("pad left")),
    ("set_name", "symbol solo", e.build_set_name("solo")),
    ("set_name", "bang", e.build_set_name("")),
    ("set_name", "abcdefghijklmnopqrstuvwxyz", e.build_set_name("abcdefghijklmnop")),
    ("get_name", "bang", e.build_get_name()),
    ("set_midi_loopback", "1", e.build_set_midi_loopback(True)),
    ("set_midi_loopback", "0", e.build_set_midi_loopback(False)),
    ("get_midi_loopback", "bang", e.build_get_midi_loopback()),
    ("set_power_save", "1", e.build_set_power_save(True)),
    ("set_power_save", "0", e.build_set_power_save(False)),
    ("get_power_save", "bang", e.build_get_power_save()),
    ("set_raw_stream", "127 1 100", e.build_set_raw_stream(127, True, 100)),
    ("set_raw_stream", "127 0 0", e.build_set_raw_stream(127, False, 0)),
    ("reset", "bang", e.build_reset()),
]

# (bytes fed into enomik.recv, expected print line or None for ignored input)
REPLIES = [
    (reply(e.RESP_GET_VERSION, 0, 19, 0), "parsed: version 0 19 0"),
    (reply(e.RESP_GET_ALL_PEERS, 3, *MAC_NIBBLES), "parsed: peer 3 84:F7:03:F2:54:62"),
    (reply(e.RESP_GET_PEER, 1, *MAC_NIBBLES), "parsed: peer 1 84:F7:03:F2:54:62"),
    (reply(e.RESP_GET_ALL_PEERS), "parsed: stream_end peers"),
    (reply(e.RESP_GET_ALL_PIN_CONFIGS, 3, 2, 0, 1, 72, 60, 0, 127), "parsed: pin_config 3 2 0 1 72 60 0 127"),
    (reply(e.RESP_GET_ALL_PIN_CONFIGS), "parsed: stream_end pins"),
    (reply(e.RESP_GET_PIN_CONFIG, 3, 2, 0, 1, 72, 60, 0, 127), "parsed: pin_config 3 2 0 1 72 60 0 127"),
    (reply(e.RESP_SET_PIN_CONFIG, 4, 5, 20, 1, 88, 7, 0, 127), "parsed: pin_config 4 5 20 1 88 7 0 127"),
    (reply(e.RESP_DELETE_PIN_CONFIG, 7), "parsed: deleted 7"),
    (reply(e.RESP_CLEAR_PIN_CONFIGS), "parsed: cleared"),
    (reply(e.RESP_GET_MAC, *MAC_NIBBLES), "parsed: mac 84:F7:03:F2:54:62"),
    (reply(e.RESP_ADD_PEER, 1), "parsed: peer_added"),
    (reply(e.RESP_RESET), "parsed: reset_done"),
    (reply(e.RESP_GET_NAME, *b"pad left"), "parsed: name pad\\ left"),
    (reply(e.RESP_SET_NAME, *b"solo"), "parsed: name solo"),
    (reply(e.RESP_SET_MIDI_LOOPBACK, 1), "parsed: loopback 1"),
    (reply(e.RESP_GET_MIDI_LOOPBACK, 0), "parsed: loopback 0"),
    (reply(e.RESP_SET_POWER_SAVE, 1), "parsed: power_save 1"),
    (reply(e.RESP_GET_POWER_SAVE, 0), "parsed: power_save 0"),
    (reply(e.RESP_SET_RAW_STREAM, 4, 1, 10), "parsed: raw_stream 4 1 10"),
    # pin 4, touch, raw 16643, baseline 132, peak 100, 40 %, norm 127, threshold 20, active
    (reply(e.CMD_RAW_VALUE, 4, 5, 0, 0, 1, 2, 3, 0, 0, 0, 1, 4, 0, 0, 0, 0, 100, 40, 127, 20, 1),
     "parsed: raw 4 5 16643 132 100 40 127 20 1"),
    (reply(e.RESP_GET_CONFIG), "parsed: config_done"),
    (reply(e.RESP_ERROR, e.CMD_GET_PIN_CONFIG, e.ERR_PIN_NOT_FOUND, 7), "errors: 2 4 7"),
    (reply(e.RESP_ERROR, e.CMD_ADD_PEER, e.ERR_PEER_ALREADY_EXISTS), "errors: 7 9"),
    (reply(0x63, 1, 2), "parsed: unknown 99 1 2"),
    ("240 1 2 3 247", None),  # other manufacturer
    ("144 60 100", None),  # note on, not SysEx
]


class PdPatch:
    """Minimal test patch: objects, message boxes and wires, with the enomik path declared."""

    def __init__(self):
        self.lines, self.wires = [], []

    def _add(self, kind: str, text: str) -> int:
        self.lines.append(f"#X {kind} 20 {20 + 30 * len(self.lines)} {text};")
        return len(self.lines) - 1

    def obj(self, text: str, source: int | None = None, outlet: int = 0) -> int:
        index = self._add("obj", text)
        if source is not None:
            self.wire(source, index, outlet)
        return index

    def send(self, messages: list[str], source: int) -> int:
        """Message box sending each 'receiver content' in order."""
        index = self._add("msg", "\\; " + " \\; ".join(messages))
        self.wire(source, index)
        return index

    def wire(self, source: int, sink: int, outlet: int = 0, inlet: int = 0) -> None:
        self.wires.append(f"#X connect {source} {outlet} {sink} {inlet};")

    def write(self, path: Path) -> Path:
        header = ["#N canvas 0 0 800 600 12;", f"#X declare -path {ENOMIK_DIR};"]
        path.write_text("\n".join(header + self.lines + self.wires) + "\n")
        return path


def run_pd(patch: Path, *flags: str, timeout: float = 30) -> list[str]:
    result = subprocess.run(
        [PD, "-nogui", "-nosound", "-stderr", *flags, "-open", str(patch)],
        capture_output=True, text=True, timeout=timeout,
    )
    return result.stderr.splitlines()


def errors(output: list[str]) -> list[str]:
    return [line for line in output if "error" in line or "couldn't" in line]


def request_patch(messages: list[str], start_delay_ms: int = 0, quit_after_ms: int = 0) -> PdPatch:
    """Every request abstraction behind [r t_<name>]; `messages` are sent on load."""
    p = PdPatch()
    for name in sorted({name for name, _, _ in REQUESTS} | {"set_raw_stream"}):
        p.obj(f"enomik.{name}", p.obj(f"r t_{name}"))
    start = p.obj("loadbang")
    if start_delay_ms:
        start = p.obj(f"delay {start_delay_ms}", start)
    if quit_after_ms:
        p.send(messages, start)
        p.send(["pd quit"], p.obj(f"delay {quit_after_ms}", p.obj("loadbang")))
    else:
        p.send(messages + ["pd quit"], start)
    return p


def framed(data: list[int]) -> list[int]:
    return [0xF0, *data, 0xF7]


@unittest.skipUnless(PD, "Pure Data not found (set PD=/path/to/pd)")
class TestPdPatches(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory(prefix="enomik_pd_")
        self.addCleanup(tmp.cleanup)
        self.tmp = Path(tmp.name)

    def test_abstractions_and_example_load(self):
        p = PdPatch()
        for path in sorted(ENOMIK_DIR.glob("enomik.*.pd")):
            p.obj(path.stem)
        p.send(["pd quit"], p.obj("loadbang"))
        self.assertEqual(errors(run_pd(p.write(self.tmp / "load.pd"), "-nomidi")), [])
        example = REPO_ROOT / "pd-patches" / "example_sysex_api.pd"
        self.assertEqual(errors(run_pd(example, "-nomidi", "-send", "pd quit")), [])

    def test_requests_match_wizard_builders(self):
        p = request_patch(["enomik.debug 1"] + [f"t_{n} {m}" for n, m, _ in REQUESTS])
        output = run_pd(p.write(self.tmp / "requests.pd"), "-nomidi")
        sent = [line for line in output if line.startswith("enomik.send:")]
        self.assertEqual(len(sent), len(REQUESTS), "\n".join(output))
        for line, (name, message, want) in zip(sent, REQUESTS):
            with self.subTest(abstraction=name, message=message):
                self.assertEqual([int(b) for b in line.split()[1:]], framed(want))

    def test_replies_are_parsed(self):
        p = PdPatch()
        recv = p.obj("enomik.recv", p.obj("r t_in"))
        p.obj("print parsed", p.obj("enomik.parse", recv))
        p.obj("print errors", recv, outlet=1)
        p.send([f"t_in {data}" for data, _ in REPLIES] + ["pd quit"], p.obj("loadbang"))
        output = run_pd(p.write(self.tmp / "replies.pd"), "-nomidi")
        printed = [line for line in output if line.startswith(("parsed:", "errors:"))]
        self.assertEqual(printed, [want for _, want in REPLIES if want])
        self.assertIn("enomik.error: PIN_NOT_FOUND 2 4 7", output)
        self.assertIn("enomik.error: PEER_ALREADY_EXISTS 7 9", output)

    def test_raw_stream_keepalive(self):
        p = request_patch(["enomik.debug 1", "t_set_raw_stream 4 1 50"], quit_after_ms=5500)
        output = run_pd(p.write(self.tmp / "keepalive.pd"), "-nomidi")
        sent = [line for line in output if line.startswith("enomik.send:")]
        self.assertEqual(len(sent), 2, "sent once, then once more after 5 s")


def virtual_midi():
    try:
        import mido

        port = mido.open_input("enomik-pd-test", virtual=True)
    except Exception:
        return None
    return port


@unittest.skipUnless(PD, "Pure Data not found (set PD=/path/to/pd)")
class TestPdMidiOutput(unittest.TestCase):
    """Bytes leave [midiout] as one SysEx message each (needs a virtual MIDI port)."""

    def test_requests_reach_midi_port(self):
        port = virtual_midi()
        if port is None:
            self.skipTest("mido/python-rtmidi with virtual ports not available")
        self.addCleanup(port.close)
        time.sleep(0.5)
        listing = "\n".join(run_pd(Path(os.devnull), "-listdev", "-send", "pd quit"))
        found = re.search(r"(\d+)\. enomik-pd-test", listing.split("MIDI output devices:")[-1])
        if not found:
            self.skipTest("Pd does not list the virtual MIDI port")
        # loadbang fires before Pd opens the MIDI device, so start late
        p = request_patch([f"t_{n} {m}" for n, m, _ in REQUESTS], start_delay_ms=500, quit_after_ms=3000)
        with tempfile.TemporaryDirectory() as tmp:
            run_pd(p.write(Path(tmp) / "midi.pd"), "-midioutdev", found.group(1))
        time.sleep(0.3)
        got = [list(m.data) for m in port.iter_pending() if m.type == "sysex"]
        self.assertEqual(got, [list(want) for _, _, want in REQUESTS])


if __name__ == "__main__":
    unittest.main()
