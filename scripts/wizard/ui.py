"""Terminal output shared by the board tester and the client_test wizard:
OK/FAIL lines, USB/Wireless chips and a block of lines redrawn in place."""

from __future__ import annotations

import sys

TTY = sys.stdout.isatty()

ENTER = ("\n", "\r")
ESC = "\x1b"

PATH_LABELS = {"usb": "USB", "wireless": "Wireless"}


def _color(text: str, code: int) -> str:
    return f"\033[{code}m{text}\033[0m" if TTY else text


def mark(ok: bool | None) -> str:
    """✔ passed, ✘ failed, · not (yet) seen."""
    if ok is None:
        return "·"
    if ok:
        return _color("✔", 32)
    return _color("✘", 31) if TTY else "✘"


def chips(paths: list[str], seen) -> str:
    """One mark per path: `seen` is a set of paths that arrived (pending = ·)
    or a dict path -> True/False/None."""
    if isinstance(seen, dict):
        state = seen
    else:
        state = {path: True for path in seen}
    return "  ".join(f"{mark(state.get(path))} {PATH_LABELS[path]}" for path in paths)


def ok_line(text: str, ok: bool) -> str:
    tag = _color("OK", 32) if ok else _color("FAIL", 31)
    return f"  [{tag}] {text}"


def section(title: str) -> None:
    print(f"\n=== {title} ===", flush=True)


class Screen:
    """A block of lines redrawn in place (on a terminal) whenever it changes."""

    def __init__(self) -> None:
        self.lines: list[str] = []

    def draw(self, lines: list[str]) -> None:
        if lines == self.lines:
            return
        if not TTY:
            # No cursor movement: keep the final state only (see done()).
            self.lines = lines
            return
        self.clear()
        print("\n".join(lines), flush=True)
        self.lines = lines

    def clear(self) -> None:
        if self.lines and TTY:
            sys.stdout.write(f"\033[{len(self.lines)}F\033[J")
        self.lines = []

    def note(self, text: str) -> None:
        """Print a line above the block; the block is drawn again below it."""
        lines = self.lines
        self.clear()
        print(text, flush=True)
        if TTY:
            self.draw(lines)
        else:
            self.lines = lines

    def done(self, lines: list[str]) -> None:
        """Leave the final state of a block in the scrollback."""
        self.clear()
        print("\n".join(lines), flush=True)
