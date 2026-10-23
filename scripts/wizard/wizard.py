#!/usr/bin/env python3
"""Board tester for the `client` sketch, the terminal twin of the enomik app's
tester page (#/tester).

Run via the isolated launcher (recommended — deps stay in scripts/wizard/.venv):

    ./run.sh

Do not run wizard.py directly unless that venv is already active.

Step by step, with the client and the dongle both on USB: set up the pins,
press the buttons (over the client's USB and wirelessly at the dongle), light
the LED through MIDI loopback, touch the pads. Then optionally write the
config the board ships with. Exits non-zero if any test failed.
"""

from __future__ import annotations

import argparse
import sys
import time

try:
    import mido
except ImportError:
    print(
        "Missing Python dependencies.\n\n"
        "Run the wizard through the isolated launcher (nothing touches system Python):\n\n"
        "  cd scripts/wizard\n"
        "  ./run.sh\n",
        file=sys.stderr,
    )
    sys.exit(1)

import enomik_sysex as sx
import tester as t
from midi_io import CLIENT_PORT_NAMES, DONGLE_PORT_NAMES, MidiLink, cbreak_stdin, find_link, poll_key

# Lets the last mark show and the button be released before moving on.
AUTO_ADVANCE_S = 0.8
# The firmware stops the raw stream after 10 s without SysEx from the host.
RAW_KEEPALIVE_S = 4.0
TTY = sys.stdout.isatty()

ENTER = ("\n", "\r")
ESC = "\x1b"


def mark(ok: bool) -> str:
    return "\033[32m✔\033[0m" if ok and TTY else ("✔" if ok else "·")


def chips(paths: list[str], seen) -> str:
    labels = {"usb": "USB", "wireless": "Wireless"}
    return "  ".join(f"{mark(path in seen)} {labels[path]}" for path in paths)


def ok_line(text: str, ok: bool) -> str:
    tag = "OK" if ok else "FAIL"
    if TTY:
        tag = f"\033[{32 if ok else 31}m{tag}\033[0m"
    return f"  [{tag}] {text}"


class Screen:
    """A block of lines redrawn in place (on a terminal) whenever it changes."""

    def __init__(self) -> None:
        self.lines: list[str] = []

    def draw(self, lines: list[str]) -> None:
        if lines == self.lines:
            return
        self.clear()
        print("\n".join(lines), flush=True)
        self.lines = lines

    def clear(self) -> None:
        if self.lines and TTY:
            sys.stdout.write(f"\033[{len(self.lines)}F\033[J")
        self.lines = []

    def note(self, text: str) -> None:
        """Print a line above the block; the next draw() puts the block back."""
        lines = self.lines
        self.clear()
        print(text, flush=True)
        if not TTY:
            return
        self.draw(lines)


class Session:
    def __init__(self, client: MidiLink, dongle: MidiLink | None, led_pin: int, touch_pins: list[int]) -> None:
        self.client = client
        self.dongle = dongle
        self.led_pin = led_pin
        self.touch_pins = touch_pins
        self.state = t.TesterState(led_pin, touch_pins)
        # Pins are set up: count MIDI and keep pin 16 configured.
        self.listening = False
        self.cycle_sent: tuple[int, ...] | None = None
        # Latest RAW_VALUE per pin from the client.
        self.raw: dict[int, dict] = {}
        self.screen = Screen()
        self.client_info = t.BoardInfo(None, None)
        self.dongle_info = t.BoardInfo(None, None)
        self.results: list[tuple[str, bool]] = []

    @property
    def paths(self) -> list[str]:
        return ["usb", "wireless"] if self.dongle else ["usb"]

    def fail(self, text: str) -> None:
        self.screen.note(ok_line(text, False))

    # --- MIDI ----------------------------------------------------------------
    def pump(self, on_sysex=None) -> None:
        """Handle everything waiting on the client (USB) and dongle (wireless) ports."""
        sources = [(self.client, "usb"), (self.dongle, "wireless")]
        for link, path in sources:
            if link is None:
                continue
            for msg in link.inport.iter_pending():
                if msg.type == "sysex":
                    data = list(msg.data)
                    if on_sysex is not None:
                        on_sysex(link, data)
                    if link is self.client:
                        reply = t.parse_reply(data)
                        if reply is not None and reply["cmd"] == "raw_value":
                            self.raw[reply["pin"]] = reply
                elif self.listening:
                    self.state.midi(path, msg.bytes())

    def request(self, link: MidiLink, data: list[int], accept, timeout: float = 2.0) -> dict | None:
        """Send a SysEx request and wait for the first reply `accept` takes, or an
        error reply for this request's command. Errors for other commands are
        ignored; MIDI arriving meanwhile still counts for the tests."""
        found: list[dict] = []
        command = data[3]

        def on_sysex(source: MidiLink, inner: list[int]) -> None:
            if source is not link or found:
                return
            reply = t.parse_reply(inner)
            if reply is None:
                return
            if reply["failed_request"] == command if reply["cmd"] == "error" else accept(reply):
                found.append(reply)

        link.send(mido.Message("sysex", data=data))
        deadline = time.monotonic() + timeout
        while not found and time.monotonic() < deadline:
            self.pump(on_sysex)
            time.sleep(0.002)
        return found[0] if found else None

    def requester(self, link: MidiLink):
        return lambda data, accept, timeout: self.request(link, data, accept, timeout)

    def sync_cycle_pin(self) -> None:
        """Pin 16 follows the state: the current MIDI type, or the LED note during
        loopback. Only changes are sent; a config the board didn't acknowledge is
        retried once."""
        while self.listening:
            data, label = self.state.cycle_config()
            key = tuple(data)
            if key == self.cycle_sent:
                return
            self.cycle_sent = key
            request = self.requester(self.client)
            if not (t.configure_pin(request, t.CYCLE_PIN, data) or t.configure_pin(request, t.CYCLE_PIN, data)):
                self.fail(f"pin {t.CYCLE_PIN} → {label}")
                return

    def tick(self) -> str | None:
        """One round of the step loops: MIDI in, pin 16 synced, a key or None."""
        self.pump()
        self.sync_cycle_pin()
        time.sleep(0.005)
        return poll_key()

    def set_loopback(self, on: bool) -> None:
        """MIDI loopback is saved on the device: on only during its step."""
        if not on:
            self.state.set_loopback(False)
        if not t.set_midi_loopback(self.requester(self.client), on):
            self.fail(f"MIDI loopback {'on' if on else 'off'}")
        if on:
            self.state.set_loopback(True)
        self.sync_cycle_pin()

    # --- Steps ---------------------------------------------------------------
    def connect(self) -> bool:
        section("1. Connect client and dongle")
        print(f"  Client: {self.client.name} · reading firmware and MAC…", flush=True)
        self.client_info = t.read_board_info(self.requester(self.client))
        print(f"  Client: {self.client.name} · {self.client_info.text()}")
        if self.dongle is None:
            print("  Dongle port not found, wireless not checked")
        else:
            self.dongle_info = t.read_board_info(self.requester(self.dongle))
            print(f"  Dongle: {self.dongle.name} · {self.dongle_info.text()}")
            if not self.dongle_info.mac:
                print(
                    "  The dongle did not report its MAC (dongle firmware before 0.19?), so it\n"
                    "  can't be added as a peer: wireless presses won't arrive."
                )
        input("\n>>> Press Enter to set up the pins (Ctrl-C to quit) ")
        while True:
            if self.setup():
                return True
            answer = input("\n>>> Setup failed: [r]etry, [c]ontinue anyway, [q]uit? ").strip().lower()
            if answer.startswith("c"):
                return True
            if answer.startswith("q"):
                return False

    def setup(self) -> bool:
        lines: list[bool] = []

        def check(text: str, ok: bool) -> None:
            lines.append(ok)
            print(ok_line(text, ok), flush=True)

        self.listening = False
        self.state = t.TesterState(self.led_pin, self.touch_pins)
        request = self.requester(self.client)
        # The dongle's own MAC, read when its port was found
        mac = self.dongle and self.dongle_info.mac
        if mac:
            check(f"add dongle {sx.mac_to_string(mac)} as peer", t.add_peer(request, mac))
        elif self.dongle:
            check("dongle MAC unknown: not added as peer", False)
        status, label = t.MIDI_TYPES[0]
        cycle = t.button_config(t.CYCLE_PIN, status)
        self.cycle_sent = tuple(cycle)
        check(
            f"pin {t.CYCLE_PIN}: input pull-up → {label} {t.CYCLE_PIN}",
            t.configure_pin(request, t.CYCLE_PIN, cycle),
        )
        for pin in t.BUTTON_PINS:
            if pin != t.CYCLE_PIN:
                check(f"pin {pin}: input pull-up → CC {pin}", t.configure_pin(request, pin, t.button_config(pin)))
        check(
            f"pin {self.led_pin}: output ← note {self.led_pin} (LED)",
            t.configure_pin(request, self.led_pin, t.led_config(self.led_pin)),
        )
        for pin in self.touch_pins:
            check(
                f"pin {pin}: touch ({t.TOUCH_THRESHOLD} %) → CC {pin}",
                t.configure_pin(request, pin, t.touch_config(pin)),
            )
        self.listening = True
        return all(lines)

    def buttons(self) -> None:
        section("2. Press the buttons")
        print(f"  Press each button. Pin {t.CYCLE_PIN} sends the next MIDI type after every release.")
        print("  Enter: next (skips while not all arrived)\n")
        state = self.state

        def render() -> list[str]:
            lines = []
            for pin, status, label in t.ROWS:
                current = pin == t.CYCLE_PIN and t.MIDI_TYPES[state.type_index][0] == status
                held = "*" if state.pressed.get(pin) else " "
                arrow = " ←" if current else ""
                lines.append(f"  {held}Pin {pin}  {label + arrow:<17} {chips(self.paths, state.buttons[(pin, status)])}")
            return lines

        self.loop_until(render, lambda: t.buttons_passed(state, self.paths))
        self.results.append(("Buttons (USB and wireless)", t.buttons_passed(state, self.paths)))

    def loopback(self) -> None:
        section("3. LED loopback")
        print(
            f"  Hold the pin {t.CYCLE_PIN} button: the LED on pin {self.led_pin} should light while\n"
            "  pressed and go off on release. The note also goes out over USB and\n"
            "  wirelessly (loopback is on during this step). Esc: skip\n"
        )
        self.set_loopback(True)
        state = self.state
        try:
            with cbreak_stdin():
                while True:
                    lines = [self.loop_note_line()]
                    if state.loop_note.released:
                        lines.append("  Did the LED light while pressed? [y/n]")
                    self.screen.draw(lines)
                    key = self.tick()
                    if key == ESC:
                        break
                    if state.loop_note.released and key in ("y", "n"):
                        state.lit = key == "y"
                        break
        finally:
            self.screen.clear()
            self.set_loopback(False)
        answer = {True: "LED lit", False: "LED not lit", None: "skipped"}[state.lit]
        print(f"{self.loop_note_line()}  {answer}")
        self.results.append(("LED loopback", t.loopback_passed(state, self.paths)))

    def loop_note_line(self) -> str:
        note = self.state.loop_note
        held = "*" if note.held and not note.released else " "
        seen = {path for path in t.PATHS if getattr(note, path)}
        return f"  {held}Note {self.led_pin:<12} {chips(self.paths, seen)}"

    def touch(self) -> None:
        section("4. Touch pins")
        print(
            "  Turn the board around and touch the solder joints of the touch pins –\n"
            "  lick your fingers first :) The bar shows the change from the untouched\n"
            f"  reading; at {t.TOUCH_THRESHOLD} % it sends CC (pin number). Enter: next\n"
        )
        state = self.state
        live = TTY and t.supports_live_values(self.client_info.version)
        if not live:
            reason = "not a terminal" if not TTY else "firmware before 0.19"
            print(f"  (no live values: {reason})\n")
        reached: set[int] = set()
        sent_at = 0.0

        def render() -> list[str]:
            lines = []
            for pin in state.touch_pins:
                value = self.raw.get(pin)
                if value is not None and value["active"]:
                    reached.add(pin)
                bar = ""
                if live:
                    if value is None:
                        bar = f"{'no live value':<20}"
                    else:
                        percent = max(0, min(100, value["percent"]))
                        filled = percent // 10
                        bar = f"[{'#' * filled}{'-' * (10 - filled)}] {percent:>3} %  "
                threshold = f"{mark(pin in reached)} threshold  " if live else ""
                lines.append(f"  Pin {pin:<3} {bar}{threshold}{chips(self.paths, state.touch.get(pin, set()))}")
            return lines

        def keepalive() -> None:
            nonlocal sent_at
            if live and time.monotonic() - sent_at >= RAW_KEEPALIVE_S:
                self.client.send(mido.Message("sysex", data=sx.build_set_raw_stream(sx.RAW_STREAM_ALL_PINS, True)))
                sent_at = time.monotonic()

        self.raw.clear()
        try:
            self.loop_until(render, lambda: t.touch_passed(state, self.paths), every_tick=keepalive)
        finally:
            if live:
                self.client.send(mido.Message("sysex", data=sx.build_set_raw_stream(sx.RAW_STREAM_ALL_PINS, False)))
        self.results.append(("Touch pins", t.touch_passed(state, self.paths)))

    def loop_until(self, render, passed, every_tick=None) -> None:
        """Redraw until Enter, or until `passed` held for AUTO_ADVANCE_S."""
        passed_at = None
        with cbreak_stdin():
            while True:
                if every_tick is not None:
                    every_tick()
                self.screen.draw(render())
                key = self.tick()
                if key in ENTER:
                    break
                if passed():
                    passed_at = passed_at or time.monotonic()
                    if time.monotonic() - passed_at >= AUTO_ADVANCE_S:
                        break
        self.print_done(render())

    def print_done(self, lines: list[str]) -> None:
        """Leave the final state of a step in the scrollback."""
        self.screen.clear()
        print("\n".join(lines), flush=True)

    def summary(self) -> int:
        section("Results")
        for label, passed in self.results:
            print(ok_line(label, passed))
        failed = sum(1 for _, passed in self.results if not passed)
        if failed:
            print(f"\n{failed} of {len(self.results)} tests failed")
        else:
            print("\nAll tests passed – the board works")
        return 1 if failed else 0

    def default_config(self) -> None:
        section("Default configuration")
        # The tested dongle stays a peer of the board
        dongle_mac = self.dongle_info.mac if self.dongle else None
        pins = " and ".join(str(pin) for pin in t.BUTTON_PINS)
        paired = (
            f"The dongle {sx.mac_to_string(dongle_mac)} stays paired."
            if dongle_mac
            else "No dongle MAC is known, so no dongle is paired."
        )
        print(
            f"  Clears the test setup and leaves the board ready to use: pins {pins} send\n"
            f"  CC {pins} (channel {t.IO_CHANNEL}), MIDI loopback is on and the LED (pin {self.led_pin})\n"
            f"  lights while the pin {t.DEFAULT_LED_SOURCE} button is held. {paired}"
        )
        if not input("\n>>> Write default configuration? [y/N] ").strip().lower().startswith("y"):
            return
        # Stop following presses: pin 16 must not be reconfigured over the default
        self.listening = False
        while True:
            lines = t.write_default_config(self.requester(self.client), self.led_pin, dongle_mac)
            for text, ok in lines:
                print(ok_line(text, ok))
            if all(ok for _, ok in lines):
                print("\nDefault configuration written")
                return
            if not input("\nDefault configuration incomplete. Try again? [y/N] ").strip().lower().startswith("y"):
                return


def section(title: str) -> None:
    print(f"\n=== {title} ===")


def open_dongle(args, client: MidiLink) -> MidiLink | None:
    """The dongle port, or None (wireless not checked). Never the client's port."""
    if args.no_dongle:
        return None
    needles = [args.dongle_port] if args.dongle_port else DONGLE_PORT_NAMES
    try:
        dongle = find_link(needles, "dongle", allow_prompt=bool(args.dongle_port))
    except RuntimeError:
        if args.dongle_port:
            raise
        return None
    if dongle.name == client.name:
        dongle.close()
        return None
    return dongle


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--client-port", help="substring of the client's MIDI port name (default: auto-detect)")
    parser.add_argument("--dongle-port", help="substring of the dongle's MIDI port name (default: auto-detect)")
    parser.add_argument("--no-dongle", action="store_true", help="test over the client's USB only")
    parser.add_argument("--led-pin", type=int, default=t.DEFAULT_LED_PIN, help="onboard LED (default %(default)s)")
    parser.add_argument(
        "--touch-pins",
        default=", ".join(map(str, t.DEFAULT_TOUCH_PINS)),
        help='comma separated, "" for none (default "%(default)s")',
    )
    args = parser.parse_args()

    try:
        touch_pins = t.parse_pins(args.touch_pins)
    except ValueError:
        parser.error("--touch-pins: pin numbers, comma separated")
    errors = t.settings_errors(args.led_pin, touch_pins)
    if errors:
        parser.error("; ".join(errors))

    print(
        "Flash the `client` sketch on the board under test and the `dongle` sketch on a\n"
        f"dongle. Wire buttons from pins {' and '.join(map(str, t.BUTTON_PINS))} to GND. Setup overwrites those pin\n"
        f"configs (note/CC = pin number, channel {t.IO_CHANNEL}). Pin {t.CYCLE_PIN} sends a different MIDI type\n"
        "on every press. Connect the dongle too: presses must arrive over the client's\n"
        "USB and wirelessly at the dongle."
    )

    client = dongle = None
    session = None
    try:
        client = find_link([args.client_port] if args.client_port else CLIENT_PORT_NAMES, "client")
        dongle = open_dongle(args, client)
        session = Session(client, dongle, args.led_pin, touch_pins)
        if not session.connect():
            return 1
        session.buttons()
        session.loopback()
        session.touch()
        code = session.summary()
        session.default_config()
        return code
    except RuntimeError as exc:
        print(f"\n{exc}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("\nStopped.")
        # Loopback is saved on the device: don't leave it on
        if session is not None and session.state.loopback_on:
            t.set_midi_loopback(session.requester(session.client), False)
        return 130
    finally:
        for link in (client, dongle):
            if link is not None:
                link.close()


if __name__ == "__main__":
    sys.exit(main())
