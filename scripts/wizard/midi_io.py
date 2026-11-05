"""Thin helpers around mido for the board tester and the client_test wizard."""

from __future__ import annotations

import select
import sys
import termios
import time
import tty
from contextlib import contextmanager
from dataclasses import dataclass

import mido

# Do not fall back to "enomik": that also matches the opposite device role.
CLIENT_PORT_NAMES = ["enomik3000_client", "client"]
DONGLE_PORT_NAMES = ["enomik3000_dongle", "dongle"]

@contextmanager
def cbreak_stdin():
    """Single-key reads (Enter, Esc, y/n) without waiting for Enter."""
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


def poll_key() -> str | None:
    """The key pressed since the last call, or None. Needs cbreak_stdin()."""
    if not sys.stdin.isatty():
        return None
    ready, _, _ = select.select([sys.stdin], [], [], 0)
    if not ready:
        return None
    return sys.stdin.read(1)


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

    def wait_for(
        self,
        predicate,
        timeout: float = 2.0,
        on_message=None,
    ):
        """Return the first matching MIDI message, or None on timeout.

        If `on_message` is set, it is called for every incoming message before
        the predicate runs (useful for logging received values).
        """
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for msg in self.inport.iter_pending():
                if on_message is not None:
                    on_message(msg)
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

    `needles` are tried in order (e.g. CLIENT_PORT_NAMES).
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
