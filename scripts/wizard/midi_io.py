"""Thin helpers around mido for the test wizard."""

from __future__ import annotations

import select
import sys
import termios
import time
import tty
from contextlib import contextmanager, nullcontext
from dataclasses import dataclass
from typing import Any

import mido

# Returned by wait_for(..., allow_esc=True) when the user presses Esc.
CANCELLED: Any = object()


@contextmanager
def _cbreak_stdin():
    """Single-key reads (Esc) without waiting for Enter."""
    if not sys.stdin.isatty():
        yield
        return
    fd = sys.stdin.fileno()
    old = termios.tcgetattr(fd)
    try:
        tty.setcbreak(fd)
        yield
    finally:
        termios.tcsetattr(fd, termios.TCSADRAIN, old)


def _poll_esc() -> bool:
    if not sys.stdin.isatty():
        return False
    ready, _, _ = select.select([sys.stdin], [], [], 0)
    if not ready:
        return False
    return sys.stdin.read(1) == "\x1b"


@dataclass
class MidiLink:
    """An open input/output pair for one device."""

    name: str
    inport: mido.ports.BaseInput
    outport: mido.ports.BaseOutput

    def close(self) -> None:
        try:
            self.inport.close()
        finally:
            self.outport.close()

    def flush(self) -> None:
        """Drop any buffered incoming messages."""
        for _ in self.inport.iter_pending():
            pass

    def send(self, msg: mido.Message) -> None:
        self.outport.send(msg)

    def wait_for(self, predicate, timeout: float = 2.0, allow_esc: bool = False):
        """Return the first matching MIDI message, None on timeout, or CANCELLED."""
        deadline = time.monotonic() + timeout
        ctx = _cbreak_stdin() if allow_esc else nullcontext()
        with ctx:
            while time.monotonic() < deadline:
                if allow_esc and _poll_esc():
                    return CANCELLED
                for msg in self.inport.iter_pending():
                    if predicate(msg):
                        return msg
                time.sleep(0.005)
        return None


def list_ports() -> tuple[list[str], list[str]]:
    return mido.get_input_names(), mido.get_output_names()


def _match(names: list[str], needles: list[str]) -> str | None:
    for needle in needles:
        for name in names:
            if needle.lower() in name.lower():
                return name
    return None


def find_link(needles: list[str], label: str, allow_prompt: bool = True) -> MidiLink:
    """Open the in/out ports for a device matched by name substrings.

    `needles` are tried in order (e.g. ["enomik3000_client", "client", "enomik"]).
    Falls back to an interactive picker when nothing matches and a TTY is present.
    """
    inputs, outputs = list_ports()
    in_name = _match(inputs, needles)
    out_name = _match(outputs, needles)

    if (in_name is None or out_name is None) and allow_prompt:
        print(f"\nCould not auto-detect the {label} MIDI port.")
        in_name = in_name or _pick("input", inputs)
        out_name = out_name or _pick("output", outputs)

    if in_name is None or out_name is None:
        raise RuntimeError(f"No MIDI ports found for {label}")

    return MidiLink(
        name=in_name,
        inport=mido.open_input(in_name),
        outport=mido.open_output(out_name),
    )


def _pick(direction: str, names: list[str]) -> str | None:
    if not names:
        print(f"  (no {direction} ports available)")
        return None
    print(f"  Available {direction} ports:")
    for i, name in enumerate(names):
        print(f"    [{i}] {name}")
    raw = input(f"  Select {direction} port index: ").strip()
    try:
        return names[int(raw)]
    except (ValueError, IndexError):
        return None
