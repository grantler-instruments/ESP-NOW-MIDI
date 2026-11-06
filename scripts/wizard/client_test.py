#!/usr/bin/env python3
"""client_test wizard: regression run of the library against the
examples/client_test firmware, laid out like the board tester (wizard.py).

Run via the isolated launcher (recommended — deps stay in scripts/wizard/.venv):

    ./run.sh client_test

Do not run client_test.py directly unless that venv is already active.

The client and the dongle are both on USB from the start. Every check is one
row with a mark per path: USB (the client's own port) and Wireless (through the
dongle, over ESP-NOW). Steps run one after the other and move on by themselves;
the real-input step waits for the buttons and the potentiometer (Enter skips).
Results at the end; exits non-zero if any check failed.
"""

from __future__ import annotations

import argparse
import sys
import time
from dataclasses import dataclass, field

try:
    import mido
except ImportError:
    print(
        "Missing Python dependencies.\n\n"
        "Run the wizard through the isolated launcher (nothing touches system Python):\n\n"
        "  cd scripts/wizard\n"
        "  ./run.sh client_test\n",
        file=sys.stderr,
    )
    sys.exit(1)

import cases as tc
import enomik_sysex as sx
from cases import (
    CC_ANALOG_IN,
    CC_ANALOG_OUT,
    CC_DIGITAL_A,
    CC_DIGITAL_B,
    ECHO_CHANNEL,
    IO_CHANNEL,
    NOTE_PIN_NOTE,
    PIN_ANALOG_IN,
    PIN_ANALOG_OUT,
    PIN_BEND,
    PIN_DIGITAL_A,
    PIN_DIGITAL_B,
    PIN_NOTE,
)
from midi_io import CLIENT_PORT_NAMES, DONGLE_PORT_NAMES, MidiLink, cbreak_stdin, find_link, poll_key
from ui import ENTER, ESC, PATH_LABELS, Screen, chips, ok_line, section

# (description, build_set_pin_config arguments)
PIN_CONFIGS = [
    (
        f"pin {PIN_DIGITAL_A} digital → CC {CC_DIGITAL_A}",
        dict(pin=PIN_DIGITAL_A, mode=sx.MODE_INPUT_PULLUP, midi_type=sx.MIDI_CONTROL_CHANGE, note_or_cc=CC_DIGITAL_A),
    ),
    (
        f"pin {PIN_DIGITAL_B} digital → CC {CC_DIGITAL_B}",
        dict(pin=PIN_DIGITAL_B, mode=sx.MODE_INPUT_PULLUP, midi_type=sx.MIDI_CONTROL_CHANGE, note_or_cc=CC_DIGITAL_B),
    ),
    (
        f"pin {PIN_ANALOG_IN} analog in → CC {CC_ANALOG_IN}",
        dict(pin=PIN_ANALOG_IN, mode=sx.MODE_ANALOG_INPUT, midi_type=sx.MIDI_CONTROL_CHANGE, note_or_cc=CC_ANALOG_IN),
    ),
    (
        f"pin {PIN_ANALOG_OUT} analog out ← CC {CC_ANALOG_OUT}",
        dict(pin=PIN_ANALOG_OUT, mode=sx.MODE_ANALOG_OUTPUT, midi_type=sx.MIDI_CONTROL_CHANGE, note_or_cc=CC_ANALOG_OUT),
    ),
    (
        f"pin {PIN_NOTE} digital → note {NOTE_PIN_NOTE}",
        dict(pin=PIN_NOTE, mode=sx.MODE_INPUT_PULLUP, midi_type=sx.MIDI_NOTE_ON, note_or_cc=NOTE_PIN_NOTE),
    ),
    (
        f"pin {PIN_BEND} analog in → pitch bend",
        dict(pin=PIN_BEND, mode=sx.MODE_ANALOG_INPUT, midi_type=sx.MIDI_PITCH_BEND, note_or_cc=0),
    ),
]

UNUSED_PIN = 47
UNUSED_COMMAND = 0x3E
BURST_VELOCITY = 99
# Lets the last mark show and the button be released before moving on.
AUTO_ADVANCE_S = 0.8
# The dongle has to discover the client (ANNOUNCE heartbeat) before it routes.
WIRELESS_LINK_TIMEOUT_S = 15.0


@dataclass
class Row:
    """One check: a mark per path (True, False, or None while pending)."""

    name: str
    paths: list[str]
    state: dict = field(default_factory=dict)
    detail: str = ""

    def set(self, path: str, ok: bool) -> None:
        self.state[path] = ok

    def finish(self) -> None:
        """Anything still pending failed."""
        for path in self.paths:
            self.state.setdefault(path, False)

    @property
    def passed(self) -> bool:
        return all(self.state.get(path) for path in self.paths)

    def line(self, width: int) -> str:
        detail = f"  {self.detail}" if self.detail else ""
        return f"  {self.name:<{width}} {chips(self.paths, self.state)}{detail}"

    def failed_paths(self) -> list[str]:
        return [path for path in self.paths if not self.state.get(path)]


@dataclass
class StepResult:
    title: str
    rows: list[Row]

    @property
    def passed(self) -> bool:
        return all(row.passed for row in self.rows)


class Session:
    def __init__(self, client: MidiLink, dongle: MidiLink | None, io_mode: str, burst: int) -> None:
        self.client = client
        self.dongle = dongle
        self.io_mode = io_mode
        self.burst = burst
        self.dongle_mac: list[int] | None = None
        self.client_version: tuple[int, int, int] | None = None
        self.results: list[StepResult] = []
        self.screen = Screen()
        self.step_number = 0

    def section(self, title: str) -> None:
        self.step_number += 1
        section(f"{self.step_number}. {title}")

    @property
    def paths(self) -> list[str]:
        return ["usb", "wireless"] if self.dongle else ["usb"]

    def link(self, path: str) -> MidiLink:
        return self.client if path == "usb" else self.dongle

    def links(self) -> list[tuple[str, MidiLink]]:
        return [(path, self.link(path)) for path in self.paths]

    # --- MIDI helpers ----------------------------------------------------------
    @staticmethod
    def send(link: MidiLink, type_: str, fields: dict) -> None:
        link.send(mido.Message(type_, **fields))

    def flush(self) -> None:
        for _, link in self.links():
            link.flush()

    def drain(self, seconds: float) -> None:
        """Let in-flight messages arrive on every port, then drop them."""
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            self.flush()
            time.sleep(0.01)

    def request(self, link: MidiLink, data: list[int], accept, timeout: float = 2.0):
        """Send SysEx and return the first parsed reply accepted by `accept`."""
        link.flush()
        link.send(mido.Message("sysex", data=data))

        def is_match(msg: mido.Message) -> bool:
            if msg.type != "sysex":
                return False
            parsed = sx.parse(list(msg.data))
            return bool(parsed and accept(parsed))

        msg = link.wait_for(is_match, timeout=timeout)
        return sx.parse(list(msg.data)) if msg else None

    def request_sysex(self, data: list[int], want_cmd: str, timeout: float = 2.0, link: MidiLink | None = None):
        return self.request(link or self.client, data, lambda p: p.get("cmd") == want_cmd, timeout)

    def request_error(self, data: list[int], failed_request: int, timeout: float = 2.0):
        return self.request(
            self.client,
            data,
            lambda p: p.get("cmd") == "error" and p.get("failed_request") == failed_request,
            timeout,
        )

    def collect_stream(self, data: list[int], end_cmd: str, timeout: float = 3.0):
        """Send a streaming request; return every parsed reply up to `end_cmd`, or None on timeout."""
        items: list[dict] = []

        def on_message(msg: mido.Message) -> None:
            if msg.type == "sysex":
                parsed = sx.parse(list(msg.data))
                if parsed:
                    items.append(parsed)

        self.client.flush()
        self.client.send(mido.Message("sysex", data=data))
        done = self.client.wait_for(
            lambda m: bool(items) and items[-1].get("cmd") == end_cmd,
            timeout=timeout,
            on_message=on_message,
        )
        return items[:-1] if done else None

    def prime_fake_inputs(self) -> None:
        """Release fake buttons and set fake analog inputs to 0, then drop what they sent.
        Unconnected analog pins read noise and would flood MIDI otherwise."""
        for pin in (PIN_DIGITAL_A, PIN_DIGITAL_B, PIN_NOTE):
            self.send(self.client, *tc.fake_release(pin))
        for pin in (PIN_ANALOG_IN, PIN_BEND):
            self.send(self.client, *tc.fake_analog(pin, 0))
        self.drain(0.5)

    # --- check lists (USB only: the dongle does not bridge configuration SysEx) --
    def check_list(self, title: str):
        """Rows printed as they complete, like the tester's setup lines."""
        rows: list[Row] = []

        def check(name: str, ok: bool, detail: str = "") -> bool:
            row = Row(name, ["usb"], {"usb": ok}, detail)
            rows.append(row)
            text = f"{name} - {detail}" if detail else name
            print(ok_line(text, ok), flush=True)
            return ok

        self.results.append(StepResult(title, rows))
        return check, rows

    def check_error(self, check, name: str, data: list[int], failed_request: int, want: int) -> bool:
        resp = self.request_error(data, failed_request)
        got = resp["error"] if resp else "no response"
        ok = bool(resp and resp["error_code"] == want)
        return check(f"error {name} → {sx.ERROR_NAMES[want]}", ok, "" if ok else f"got {got}")

    # --- rows with a mark per path ----------------------------------------------
    def row_step(self, title: str, rows: list[Row], run_row) -> None:
        """Run `run_row(index, row, redraw)` for each row, redrawing the block as marks arrive."""
        width = max(len(row.name) for row in rows)
        render = lambda: [row.line(width) for row in rows]  # noqa: E731
        for index, row in enumerate(rows):
            self.screen.draw(render())
            run_row(index, row, lambda: self.screen.draw(render()))
            row.finish()
        self.screen.done(render())
        self.results.append(StepResult(title, rows))

    def expect_on_each_path(self, row: Row, redraw, case: tc.MidiCase, timeout: float = 3.0) -> None:
        """Send `case` into each path and expect the echo back on the same path."""
        for path, link in self.links():
            self.flush()
            self.send(link, case.type, case.message_kwargs())
            row.set(path, link.wait_for(case.matches, timeout=timeout) is not None)
            redraw()
            if case.type == "note_on":  # release notes so later tests start clean
                self.send(link, "note_off", {"channel": case.channel - 1, "note": case.fields["note"], "velocity": 0})

    def expect_after_trigger(self, row: Row, redraw, trigger: tuple[str, dict], predicate, timeout: float = 3.0) -> None:
        """Send `trigger` to the client over USB; the client sends to USB and
        ESP-NOW at once, so the result is expected on every path."""
        self.flush()
        self.send(self.client, *trigger)
        pending = dict(self.links())
        deadline = time.monotonic() + timeout
        while pending and time.monotonic() < deadline:
            for path, link in list(pending.items()):
                for msg in link.inport.iter_pending():
                    if predicate(msg):
                        row.set(path, True)
                        del pending[path]
                        redraw()
                        break
            time.sleep(0.002)

    # --- 1. Connect ---------------------------------------------------------------
    def connect(self, cli_mac: list[int] | None) -> bool:
        self.section("Connect client and dongle")
        resp = self.request_sysex(sx.build_get_version(), "version")
        if resp:
            self.client_version = (resp["major"], resp["minor"], resp["patch"])
        mac = self.request_sysex(sx.build_get_mac(), "get_mac")
        print(f"  Client: {self.client.name} · {board_text(self.client_version, mac and mac['mac'])}")

        if self.dongle is None:
            print("  Dongle port not found, wireless not checked")
        else:
            version = self.request_sysex(sx.build_get_version(), "version", link=self.dongle)
            mac = self.request_sysex(sx.build_get_mac(), "get_mac", link=self.dongle)
            dongle_version = (version["major"], version["minor"], version["patch"]) if version else None
            print(f"  Dongle: {self.dongle.name} · {board_text(dongle_version, mac and mac['mac'])}")
            self.dongle_mac = cli_mac or (mac and mac["mac"])
            if cli_mac:
                print(f"  Dongle MAC from --dongle-mac: {sx.mac_to_string(cli_mac)}")
            if not self.dongle_mac:
                self.dongle_mac = ask_dongle_mac()

        input("\n>>> Press Enter to set up the client (Ctrl-C to quit) ")
        while True:
            if self.setup():
                return True
            answer = input("\n>>> Setup failed: [r]etry, [c]ontinue anyway, [q]uit? ").strip().lower()
            if answer.startswith("c"):
                return True
            if answer.startswith("q"):
                return False
            self.results.pop()

    def setup(self) -> bool:
        check, rows = self.check_list("Setup")
        version = self.client_version
        check(
            "firmware matches the wizard",
            bool(version and version[:2] == (sx.PROTOCOL_MAJOR, sx.PROTOCOL_MINOR)),
            "" if version and version[:2] == (sx.PROTOCOL_MAJOR, sx.PROTOCOL_MINOR)
            else f"client {fmt_version(version)}, wizard {sx.PROTOCOL_MAJOR}.{sx.PROTOCOL_MINOR}",
        )
        # Clean slate so the run is reproducible.
        check("RESET", self.request_sysex(sx.build_reset(), "reset") is not None)
        self.prime_fake_inputs()
        for name, args in PIN_CONFIGS:
            resp = self.request_sysex(sx.build_set_pin_config(channel=IO_CHANNEL, **args), "pin_config")
            check(name, bool(
                resp
                and resp["pin"] == args["pin"]
                and resp["mode"] == args["mode"]
                and resp["channel"] == IO_CHANNEL
                and resp["midi_type"] == args["midi_type"]
                and resp["note_or_cc"] == args["note_or_cc"]
            ))
        if self.dongle:
            if self.dongle_mac:
                mac_text = sx.mac_to_string(self.dongle_mac)
                if check(f"add dongle {mac_text} as peer",
                         self.request_sysex(sx.build_add_peer(self.dongle_mac), "add_peer_ok") is not None):
                    print("  waiting for the dongle to reach the client…", flush=True)
                    check("wireless link to the client", self.wait_for_wireless())
            else:
                check("dongle MAC unknown: not added as peer", False)
        return all(row.passed for row in rows)

    def wait_for_wireless(self) -> bool:
        """An echo comes back through the dongle (the dongle has to discover the
        client first). Fake inputs stay set meanwhile: after a reboot the client
        reads its real pins, and unconnected analog pins flood MIDI."""
        case = tc.channel_voice_cases()[0]
        deadline = time.monotonic() + WIRELESS_LINK_TIMEOUT_S
        got = None
        while got is None and time.monotonic() < deadline:
            self.prime_fake_inputs()
            self.send(self.dongle, case.type, case.message_kwargs())
            got = self.dongle.wait_for(case.matches, timeout=1.0)
            self.send(self.dongle, "note_off", {"channel": case.channel - 1, "note": case.fields["note"], "velocity": 0})
        self.prime_fake_inputs()
        return got is not None

    # --- 2. SysEx -------------------------------------------------------------------
    def sysex(self) -> None:
        self.section("SysEx over USB")
        print("  (the dongle does not bridge configuration SysEx)")
        check, _ = self.check_list("SysEx")

        # MIDI loopback: outgoing Client MIDI is fed back to receive handlers
        # (so pin 17 → CC 17 drives analog out pin 21 without a host round-trip).
        resp = self.request_sysex(sx.build_set_midi_loopback(True), "midi_loopback")
        check("SET_MIDI_LOOPBACK on", bool(resp and resp["enabled"]))
        resp = self.request_sysex(sx.build_get_midi_loopback(), "midi_loopback")
        check("GET_MIDI_LOOPBACK", bool(resp and resp["enabled"]))

        for enabled in (True, False):
            word = "on" if enabled else "off"
            resp = self.request_sysex(sx.build_set_power_save(enabled), "power_save")
            check(f"SET_POWER_SAVE {word}", bool(resp and resp["enabled"] == enabled))
            resp = self.request_sysex(sx.build_get_power_save(), "power_save")
            check(f"GET_POWER_SAVE {word}", bool(resp and resp["enabled"] == enabled))

        resp = self.request_sysex(sx.build_set_name("wizard"), "name")
        check("SET_NAME", bool(resp and resp["name"] == "wizard"), resp["name"] if resp else "")
        resp = self.request_sysex(sx.build_get_name(), "name")
        check("GET_NAME", bool(resp and resp["name"] == "wizard"), resp["name"] if resp else "")
        resp = self.request_sysex(sx.build_set_name(""), "name")
        check(
            "SET_NAME empty restores the default",
            bool(resp and resp["name"] and resp["name"] != "wizard"),
            resp["name"] if resp else "",
        )

        resp = self.request_sysex(sx.build_get_pin_config(PIN_ANALOG_IN), "pin_config")
        check(f"GET_PIN_CONFIG pin {PIN_ANALOG_IN}",
              bool(resp and resp["pin"] == PIN_ANALOG_IN and resp["note_or_cc"] == CC_ANALOG_IN))
        items = self.collect_stream(sx.build_get_all_pin_configs(), "pin_config_stream_end")
        pins = sorted(i["pin"] for i in items or [] if i.get("cmd") == "pin_config")
        want = sorted(args["pin"] for _, args in PIN_CONFIGS)
        check("GET_ALL_PIN_CONFIGS", pins == want, f"pins {pins}")

        self.check_error(check, "unknown command", sx.build_raw(UNUSED_COMMAND), UNUSED_COMMAND,
                         sx.ERR_UNKNOWN_COMMAND)
        self.check_error(check, "wrong protocol major",
                         sx.build_raw(sx.CMD_GET_VERSION, major=sx.PROTOCOL_MAJOR + 1),
                         sx.CMD_GET_VERSION, sx.ERR_BAD_VERSION)
        self.check_error(check, "unconfigured pin", sx.build_get_pin_config(UNUSED_PIN),
                         sx.CMD_GET_PIN_CONFIG, sx.ERR_PIN_NOT_FOUND)
        self.check_error(check, "short MAC", sx.build_raw(sx.CMD_ADD_PEER, [1, 2, 3]),
                         sx.CMD_ADD_PEER, sx.ERR_DECODE_FAILED)

        self.raw_stream(check)
        if self.dongle and self.dongle_mac:
            self.peers(check)
        self.prime_fake_inputs()

    def raw_stream(self, check) -> None:
        """SET_RAW_STREAM on the fake analog pin: frames arrive, follow a threshold change, stop."""
        args = dict(next(a for _, a in PIN_CONFIGS if a["pin"] == PIN_ANALOG_IN))
        self.send(self.client, *tc.fake_analog(PIN_ANALOG_IN, 64))
        self.drain(0.3)

        resp = self.request_sysex(sx.build_set_raw_stream(PIN_ANALOG_IN, True, 50), "raw_stream")
        check("SET_RAW_STREAM on",
              bool(resp and resp["pin"] == PIN_ANALOG_IN and resp["enabled"] and resp["interval_ms"] == 50))

        frames = self.raw_frames(1.0)
        norms = [f["norm"] for f in frames]
        ok = 14 <= len(frames) <= 24 and all(f["pin"] == PIN_ANALOG_IN for f in frames) and \
            bool(norms) and abs(norms[-1] - 64) <= 2
        check("RAW_VALUE frames at 50 ms", ok, f"{len(frames)} frames, norm {norms[-1:]}")

        # Tuning the threshold keeps the stream running.
        self.request_sysex(sx.build_set_pin_config(channel=IO_CHANNEL, threshold=33, **args), "pin_config")
        frames = self.raw_frames(0.3)
        check("RAW_VALUE shows new threshold", bool(frames) and frames[-1]["threshold"] == 33)
        self.request_sysex(sx.build_set_pin_config(channel=IO_CHANNEL, **args), "pin_config")

        resp = self.request_sysex(sx.build_set_raw_stream(PIN_ANALOG_IN, False), "raw_stream")
        check("SET_RAW_STREAM off", bool(resp and not resp["enabled"]))
        self.drain(0.2)
        frames = self.raw_frames(0.5)
        check("RAW_VALUE frames stop", not frames, f"{len(frames)} after off")

        self.check_error(check, "raw stream on unconfigured pin", sx.build_set_raw_stream(UNUSED_PIN, True),
                         sx.CMD_SET_RAW_STREAM, sx.ERR_PIN_NOT_FOUND)

    def raw_frames(self, seconds: float) -> list[dict]:
        frames: list[dict] = []

        def on_message(msg: mido.Message) -> None:
            if msg.type == "sysex":
                parsed = sx.parse(list(msg.data))
                if parsed and parsed.get("cmd") == "raw_value":
                    frames.append(parsed)

        self.client.flush()
        self.client.wait_for(lambda m: False, timeout=seconds, on_message=on_message)
        return frames

    def peers(self, check) -> None:
        mac_text = sx.mac_to_string(self.dongle_mac)
        self.check_error(check, "duplicate ADD_PEER", sx.build_add_peer(self.dongle_mac),
                         sx.CMD_ADD_PEER, sx.ERR_PEER_ALREADY_EXISTS)
        resp = self.request_sysex(sx.build_get_peer(0), "peer_entry")
        check("GET_PEER 0", bool(resp and resp["mac"] == self.dongle_mac), mac_text)
        items = self.collect_stream(sx.build_get_all_peers(), "peer_stream_end")
        macs = [i["mac"] for i in items or [] if i.get("cmd") == "peer_entry"]
        check("GET_ALL_PEERS", self.dongle_mac in macs, f"{len(macs)} peer(s)")
        items = self.collect_stream(sx.build_get_config(), "get_config_ok")
        kinds = {i.get("cmd") for i in items or []}
        pins = sum(1 for i in items or [] if i.get("cmd") == "pin_config")
        ok = items is not None and pins == len(PIN_CONFIGS) and \
            {"peer_entry", "midi_loopback", "power_save", "name"} <= kinds
        check("GET_CONFIG stream", ok, f"{pins} pins")

    # --- 3. Echo --------------------------------------------------------------------
    def echo(self) -> None:
        self.section("Echo")
        print(f"  Every message goes in over each path; the client echoes channel {ECHO_CHANNEL}\n"
              "  and system messages back.\n")
        cases = tc.channel_voice_cases() + tc.system_cases()
        rows = [Row(case.name, self.paths) for case in cases]
        rows.append(Row(f"no echo on ch {IO_CHANNEL}", self.paths))

        def run(index: int, row: Row, redraw) -> None:
            if index < len(cases):
                self.expect_on_each_path(row, redraw, cases[index])
            else:
                self.no_echo_on_io_channel(row, redraw)

        self.row_step("Echo", rows, run)

    def no_echo_on_io_channel(self, row: Row, redraw) -> None:
        """Traffic on IO_CHANNEL must not be mirrored back."""
        for path, link in self.links():
            self.flush()
            self.send(link, "note_on", {"channel": IO_CHANNEL - 1, "note": 99, "velocity": 1})
            stray = link.wait_for(lambda m: m.type == "note_on" and m.note == 99, timeout=0.5)
            row.set(path, stray is None)
            redraw()
            self.send(link, "note_off", {"channel": IO_CHANNEL - 1, "note": 99, "velocity": 0})

    # --- 4. Burst -------------------------------------------------------------------
    def burst_step(self) -> None:
        if self.burst <= 0:
            return
        self.section("Burst")
        print(f"  {self.burst} notes back to back over each path; all must come back over USB,\n"
              "  up to 5 % may get lost wirelessly.\n")
        rows = [Row(f"{self.burst} notes", [path]) for path in self.paths]
        settings = {"usb": (0.002, 0.0), "wireless": (0.005, 0.05)}

        def run(_: int, row: Row, redraw) -> None:
            path = row.paths[0]
            spacing, max_loss = settings[path]
            stats = self.burst_on(self.link(path), spacing)
            row.detail = f"{stats['received']}/{stats['sent']} back"
            if stats["avg_ms"] is not None:
                row.detail += (f", round trip avg {stats['avg_ms']:.1f} ms, "
                               f"p95 {stats['p95_ms']:.1f} ms, max {stats['max_ms']:.1f} ms")
            row.set(path, stats["loss"] <= max_loss)
            redraw()

        self.row_step("Burst", rows, run)

    def burst_on(self, link: MidiLink, spacing: float) -> dict:
        """Send notes back to back and see how many echoes return, and how fast."""
        channel = ECHO_CHANNEL - 1
        sent: dict[int, float] = {}
        received: dict[int, float] = {}

        def on_message(msg: mido.Message) -> None:
            if (
                msg.type == "note_on"
                and msg.channel == channel
                and msg.velocity == BURST_VELOCITY
                and msg.note in sent
                and msg.note not in received
            ):
                received[msg.note] = time.monotonic()

        self.flush()
        for note in tc.burst_notes(self.burst):
            sent[note] = time.monotonic()
            self.send(link, "note_on", {"channel": channel, "note": note, "velocity": BURST_VELOCITY})
            link.wait_for(lambda m: False, timeout=spacing, on_message=on_message)
        if len(received) < len(sent):
            link.wait_for(lambda m: len(received) == len(sent), timeout=3.0, on_message=on_message)
        for note in sent:
            self.send(link, "note_off", {"channel": channel, "note": note, "velocity": 0})
        self.drain(0.5)
        return tc.burst_stats(sent, received)

    # --- 5. API sends -----------------------------------------------------------------
    def api_sends(self) -> None:
        self.section("Client send API")
        print("  The client calls each enomik::Client send function; it goes out over USB\n"
              "  and ESP-NOW at once.\n")
        rows = [Row(case.name, self.paths) for case in tc.API_CASES]

        def run(index: int, row: Row, redraw) -> None:
            self.expect_after_trigger(row, redraw, tc.api_trigger(index), tc.API_CASES[index].matches)

        self.row_step("Client send API", rows, run)
        self.drain(0.3)

    # --- 6. Fake inputs -------------------------------------------------------------
    def fake_inputs(self) -> None:
        self.section("Fake inputs")
        print(f"  The wizard sets what the input pins read (channel {tc.FAKE_CHANNEL}); the client\n"
              "  sends the MIDI configured for them.\n")
        steps: list[tuple[str, tuple[str, dict], object]] = []
        for pin, cc in ((PIN_DIGITAL_A, CC_DIGITAL_A), (PIN_DIGITAL_B, CC_DIGITAL_B)):
            # INPUT_PULLUP is active low: press sends max (127), release min (0).
            steps.append((f"press pin {pin} → CC {cc} = 127", tc.fake_press(pin), tc.io_cc(cc, 127).matches))
            steps.append((f"release pin {pin} → CC {cc} = 0", tc.fake_release(pin), tc.io_cc(cc, 0).matches))
        note_on = tc.MidiCase("note", "note_on", {"note": NOTE_PIN_NOTE, "velocity": 127}, channel=IO_CHANNEL)
        steps.append((f"press pin {PIN_NOTE} → note on {NOTE_PIN_NOTE}", tc.fake_press(PIN_NOTE), note_on.matches))
        steps.append((f"release pin {PIN_NOTE} → note off {NOTE_PIN_NOTE}", tc.fake_release(PIN_NOTE),
                      lambda m: tc.is_note_release(m, NOTE_PIN_NOTE)))
        for level in tc.ANALOG_SWEEP:
            steps.append((f"analog pin {PIN_ANALOG_IN} → CC {CC_ANALOG_IN} = {level}",
                          tc.fake_analog(PIN_ANALOG_IN, level), tc.io_cc(CC_ANALOG_IN, level).matches))
        for level, pitch, tolerance in tc.BEND_SWEEP:
            steps.append((f"analog pin {PIN_BEND} → pitch bend {pitch}",
                          tc.fake_analog(PIN_BEND, level), tc.io_bend(pitch, tolerance).matches))

        rows = [Row(name, self.paths) for name, _, _ in steps]
        self.prime_fake_inputs()

        def run(index: int, row: Row, redraw) -> None:
            _, trigger, predicate = steps[index]
            self.expect_after_trigger(row, redraw, trigger, predicate)

        self.row_step("Fake inputs", rows, run)
        self.prime_fake_inputs()

    # --- 7. Real inputs -------------------------------------------------------------
    def real_inputs(self) -> None:
        self.section("Buttons and potentiometer")
        print(
            f"  Press the buttons on pins {PIN_DIGITAL_A} and {PIN_DIGITAL_B}, then turn the potentiometer on\n"
            f"  pin {PIN_ANALOG_IN} all the way left (0), to the middle (64) and all the way right (127).\n"
            "  Moves on once everything arrived; Enter: skip what is missing\n"
        )
        mido_ch = IO_CHANNEL - 1
        buttons = [(Row(f"button pin {pin} → CC {cc}", self.paths), cc)
                   for pin, cc in ((PIN_DIGITAL_A, CC_DIGITAL_A), (PIN_DIGITAL_B, CC_DIGITAL_B))]
        pot = [(Row(f"potentiometer pin {PIN_ANALOG_IN} → CC {CC_ANALOG_IN} = {value}", self.paths), value)
               for value in (0, 64, 127)]
        rows = [row for row, _ in buttons + pot]
        width = max(len(row.name) for row in rows)
        last_value: list[int | None] = [None]

        def on_message(path: str, msg: mido.Message) -> None:
            if msg.type != "control_change" or msg.channel != mido_ch:
                return
            for row, cc in buttons:
                # INPUT_PULLUP active low: a press sends 127.
                if msg.control == cc and msg.value > 0:
                    row.set(path, True)
            if msg.control == CC_ANALOG_IN:
                last_value[0] = msg.value
                for row, value in pot:
                    if msg.value == value:
                        row.set(path, True)

        def render() -> list[str]:
            lines = [row.line(width) for row in rows]
            now = "–" if last_value[0] is None else last_value[0]
            lines.append(f"  potentiometer now: {now}")
            return lines

        self.send(self.client, *tc.fake_clear())  # read the real pins again
        self.drain(0.3)
        passed_at = None
        try:
            with cbreak_stdin():
                while True:
                    for path, link in self.links():
                        for msg in link.inport.iter_pending():
                            on_message(path, msg)
                    self.screen.draw(render())
                    if poll_key() in ENTER + (ESC,):
                        break
                    if all(row.passed for row in rows):
                        passed_at = passed_at or time.monotonic()
                        if time.monotonic() - passed_at >= AUTO_ADVANCE_S:
                            break
                    time.sleep(0.005)
        finally:
            for row in rows:
                row.finish()
            self.screen.done(render()[:-1])
            self.prime_fake_inputs()  # quiet again: unconnected pins read noise
        self.results.append(StepResult("Buttons and potentiometer", rows))

    # --- Results --------------------------------------------------------------------
    def summary(self) -> int:
        section("Results")
        failed_steps = 0
        for step in self.results:
            passed = sum(1 for row in step.rows if row.passed)
            print(ok_line(f"{step.title} ({passed}/{len(step.rows)})", step.passed))
            for row in step.rows:
                if not row.passed:
                    # Which path failed, when there is more than one
                    where = ""
                    if len(row.paths) > 1:
                        where = ": " + ", ".join(PATH_LABELS[path] for path in row.failed_paths())
                    detail = f" - {row.detail}" if row.detail else ""
                    print(f"         {row.name}{where}{detail}")
            failed_steps += not step.passed
        if failed_steps:
            print(f"\n{failed_steps} of {len(self.results)} steps failed")
        else:
            print("\nAll tests passed")
        return 1 if failed_steps else 0


def fmt_version(version) -> str:
    return ".".join(map(str, version)) if version else "unknown"


def board_text(version, mac) -> str:
    return f"firmware {fmt_version(version)} · MAC {sx.mac_to_string(mac) if mac else 'unknown'}"


def ask_dongle_mac() -> list[int] | None:
    print(
        "  The dongle did not report its MAC (dongle firmware before 0.19?).\n"
        "  Enter it to pair (print_mac sketch, dongle display or serial log),\n"
        "  or leave blank: then nothing arrives wirelessly."
    )
    while True:
        raw = input("  Dongle MAC [xx:xx:xx:xx:xx:xx]: ").strip()
        if not raw:
            return None
        try:
            return sx.mac_from_string(raw)
        except ValueError as exc:
            print(f"  Invalid MAC: {exc}")


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
    parser.add_argument(
        "--dongle-mac",
        metavar="MAC",
        help="dongle Wi-Fi STA MAC (e.g. 84:F7:03:F2:54:62); default: read from the dongle, else ask",
    )
    parser.add_argument(
        "--io",
        choices=["virtual", "physical", "both"],
        default="both",
        help="input tests: fake inputs in the firmware, real buttons/pot, or both (default)",
    )
    parser.add_argument(
        "--burst",
        type=int,
        default=100,
        metavar="N",
        help="notes in the burst test, 1..128 (0 = skip, default 100)",
    )
    args = parser.parse_args()

    if not 0 <= args.burst <= 128:
        parser.error("--burst must be 0..128")
    cli_mac = None
    if args.dongle_mac:
        try:
            cli_mac = sx.mac_from_string(args.dongle_mac.strip())
        except ValueError as exc:
            parser.error(f"--dongle-mac: {exc}")

    print(
        "Flash examples/client_test on the client (ESP32-S2/S3, USB) and the `dongle`\n"
        "sketch (0.19 or later) on a dongle; connect both over USB. Every check runs\n"
        "over the client's USB and wirelessly through the dongle."
    )
    if args.io != "virtual":
        print(
            f"Wiring for step 7: buttons from pins {PIN_DIGITAL_A} and {PIN_DIGITAL_B} to GND, a potentiometer\n"
            f"on pin {PIN_ANALOG_IN} (ends to 3V3 and GND), optional LED on pin {PIN_ANALOG_OUT}\n"
            f"(follows the pin {PIN_DIGITAL_B} button through loopback). No wiring: --io virtual"
        )

    client = dongle = None
    try:
        client = find_link([args.client_port] if args.client_port else CLIENT_PORT_NAMES, "client")
        dongle = open_dongle(args, client)
        session = Session(client, dongle, args.io, args.burst)
        if not session.connect(cli_mac):
            return 1
        session.sysex()
        session.echo()
        session.burst_step()
        session.api_sends()
        if args.io in ("virtual", "both"):
            session.fake_inputs()
        if args.io in ("physical", "both"):
            session.real_inputs()
        return session.summary()
    except RuntimeError as exc:
        print(f"\n{exc}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("\nStopped.")
        return 130
    finally:
        for link in (client, dongle):
            if link is not None:
                link.close()


if __name__ == "__main__":
    sys.exit(main())
