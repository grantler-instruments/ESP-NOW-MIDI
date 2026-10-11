#!/usr/bin/env python3
"""Interactive test wizard for the ESP-NOW-MIDI client_test firmware.

Run via the isolated launcher (recommended — deps stay in scripts/wizard/.venv):

    ./run.sh

Do not run wizard.py directly unless that venv is already active.

Phase 1 - the client board connected directly via USB MIDI
Phase 2 - the client reached wirelessly through the dongle

Each step performs an automatic check and reports PASS / FAIL. Inputs are
tested with fake values from the firmware, then with real buttons and a
potentiometer (Esc skips a step). The process exits non-zero if any step fails.
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
from midi_io import CANCELLED, MidiLink, find_link

PASS = "PASS"
FAIL = "FAIL"

# (description, build_set_pin_config arguments)
PIN_CONFIGS = [
    (
        f"pin {PIN_DIGITAL_A} digital -> CC {CC_DIGITAL_A}",
        dict(pin=PIN_DIGITAL_A, mode=sx.MODE_INPUT_PULLUP, midi_type=sx.MIDI_CONTROL_CHANGE, note_or_cc=CC_DIGITAL_A),
    ),
    (
        f"pin {PIN_DIGITAL_B} digital -> CC {CC_DIGITAL_B}",
        dict(pin=PIN_DIGITAL_B, mode=sx.MODE_INPUT_PULLUP, midi_type=sx.MIDI_CONTROL_CHANGE, note_or_cc=CC_DIGITAL_B),
    ),
    (
        f"pin {PIN_ANALOG_IN} analog in -> CC {CC_ANALOG_IN}",
        dict(pin=PIN_ANALOG_IN, mode=sx.MODE_ANALOG_INPUT, midi_type=sx.MIDI_CONTROL_CHANGE, note_or_cc=CC_ANALOG_IN),
    ),
    (
        f"pin {PIN_ANALOG_OUT} analog out <- CC {CC_ANALOG_OUT}",
        dict(pin=PIN_ANALOG_OUT, mode=sx.MODE_ANALOG_OUTPUT, midi_type=sx.MIDI_CONTROL_CHANGE, note_or_cc=CC_ANALOG_OUT),
    ),
    (
        f"pin {PIN_NOTE} digital -> note {NOTE_PIN_NOTE}",
        dict(pin=PIN_NOTE, mode=sx.MODE_INPUT_PULLUP, midi_type=sx.MIDI_NOTE_ON, note_or_cc=NOTE_PIN_NOTE),
    ),
    (
        f"pin {PIN_BEND} analog in -> pitch bend",
        dict(pin=PIN_BEND, mode=sx.MODE_ANALOG_INPUT, midi_type=sx.MIDI_PITCH_BEND, note_or_cc=0),
    ),
]

UNUSED_PIN = 47
UNUSED_COMMAND = 0x3E
BURST_VELOCITY = 99


class Wizard:
    def __init__(self, io_mode: str = "both", burst: int = 100) -> None:
        self.results: list[tuple[str, bool]] = []
        self.dongle_mac: list[int] | None = None
        self.io_mode = io_mode
        self.burst = burst

    # --- output helpers ------------------------------------------------------
    def record(self, name: str, ok: bool, detail: str = "") -> bool:
        tag = PASS if ok else FAIL
        line = f"  [{tag}] {name}"
        if detail:
            line += f" - {detail}"
        print(line)
        self.results.append((name, ok))
        return ok

    @staticmethod
    def section(title: str) -> None:
        print(f"\n=== {title} ===")

    @staticmethod
    def prompt(text: str) -> None:
        input(f"\n>>> {text} (press Enter to continue) ")

    @staticmethod
    def send(link: MidiLink, type_: str, fields: dict) -> None:
        link.send(mido.Message(type_, **fields))

    @staticmethod
    def drain(link: MidiLink, seconds: float) -> None:
        """Let in-flight messages arrive, then drop them."""
        link.wait_for(lambda m: False, timeout=seconds)
        link.flush()

    # --- sysex helpers -------------------------------------------------------
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

    def request_sysex(self, link: MidiLink, data: list[int], want_cmd: str, timeout: float = 2.0):
        return self.request(link, data, lambda p: p.get("cmd") == want_cmd, timeout)

    def request_error(self, link: MidiLink, data: list[int], failed_request: int, timeout: float = 2.0):
        return self.request(
            link,
            data,
            lambda p: p.get("cmd") == "error" and p.get("failed_request") == failed_request,
            timeout,
        )

    def collect_stream(self, link: MidiLink, data: list[int], end_cmd: str, timeout: float = 3.0):
        """Send a streaming request; return every parsed reply up to `end_cmd`, or None on timeout."""
        items: list[dict] = []

        def on_message(msg: mido.Message) -> None:
            if msg.type == "sysex":
                parsed = sx.parse(list(msg.data))
                if parsed:
                    items.append(parsed)

        link.flush()
        link.send(mido.Message("sysex", data=data))
        done = link.wait_for(
            lambda m: bool(items) and items[-1].get("cmd") == end_cmd,
            timeout=timeout,
            on_message=on_message,
        )
        return items[:-1] if done else None

    def check_error(self, link: MidiLink, name: str, data: list[int], failed_request: int, want: int) -> bool:
        resp = self.request_error(link, data, failed_request)
        got = resp["error"] if resp else "no response"
        ok = bool(resp and resp["error_code"] == want)
        return self.record(f"SysEx error {name} -> {sx.ERROR_NAMES[want]}", ok, "" if ok else f"got {got}")

    # --- phase 1: USB --------------------------------------------------------
    def phase1(self, link: MidiLink) -> None:
        self.section("Phase 1 - Client via USB MIDI")

        # Clean slate so the run is reproducible.
        self.request_sysex(link, sx.build_reset(), "reset", timeout=2.0)

        self._sysex_basics(link)
        if self.io_mode in ("virtual", "both"):
            self._prime_fake_inputs(link)
        self._configure_pins(link)
        self._sysex_errors(link)

        self._echo(link, tc.channel_voice_cases(), "USB")
        self._no_echo_on_io_channel(link, "USB")
        self._echo(link, tc.system_cases(), "USB")
        self._burst_test(link, "USB", spacing=0.002, max_loss=0.0)

        self._pair_with_dongle(link)
        self._api_sends(link, "USB")
        self._io_tests(link, "USB")

    def _sysex_basics(self, link: MidiLink) -> None:
        resp = self.request_sysex(link, sx.build_get_version(), "version")
        ok = bool(resp and resp["major"] == sx.PROTOCOL_MAJOR and resp["minor"] == sx.PROTOCOL_MINOR)
        detail = f"{resp['major']}.{resp['minor']}.{resp['patch']}" if resp else "no response"
        self.record("SysEx GET_VERSION matches the wizard", ok, detail)

        # GET_MAC proves SysEx works in both directions.
        resp = self.request_sysex(link, sx.build_get_mac(), "get_mac")
        self.record("SysEx GET_MAC round-trip", bool(resp), sx.mac_to_string(resp["mac"]) if resp else "no response")

        # MIDI loopback: outgoing Client MIDI is fed back to receive handlers
        # (so pin 17 -> CC 17 drives analog out pin 21 without a host round-trip).
        resp = self.request_sysex(link, sx.build_set_midi_loopback(True), "midi_loopback")
        self.record("SysEx SET_MIDI_LOOPBACK on", bool(resp and resp["enabled"]))
        resp = self.request_sysex(link, sx.build_get_midi_loopback(), "midi_loopback")
        self.record("SysEx GET_MIDI_LOOPBACK", bool(resp and resp["enabled"]))

        for enabled in (True, False):
            resp = self.request_sysex(link, sx.build_set_power_save(enabled), "power_save")
            self.record(f"SysEx SET_POWER_SAVE {'on' if enabled else 'off'}", bool(resp and resp["enabled"] == enabled))
            resp = self.request_sysex(link, sx.build_get_power_save(), "power_save")
            self.record(f"SysEx GET_POWER_SAVE {'on' if enabled else 'off'}", bool(resp and resp["enabled"] == enabled))

        resp = self.request_sysex(link, sx.build_set_name("wizard"), "name")
        self.record("SysEx SET_NAME", bool(resp and resp["name"] == "wizard"), resp["name"] if resp else "")
        resp = self.request_sysex(link, sx.build_get_name(), "name")
        self.record("SysEx GET_NAME", bool(resp and resp["name"] == "wizard"), resp["name"] if resp else "")
        resp = self.request_sysex(link, sx.build_set_name(""), "name")
        self.record(
            "SysEx SET_NAME empty restores the default",
            bool(resp and resp["name"] and resp["name"] != "wizard"),
            resp["name"] if resp else "",
        )

    def _configure_pins(self, link: MidiLink) -> None:
        for name, args in PIN_CONFIGS:
            resp = self.request_sysex(
                link, sx.build_set_pin_config(channel=IO_CHANNEL, **args), "pin_config"
            )
            ok = bool(
                resp
                and resp["pin"] == args["pin"]
                and resp["mode"] == args["mode"]
                and resp["channel"] == IO_CHANNEL
                and resp["midi_type"] == args["midi_type"]
                and resp["note_or_cc"] == args["note_or_cc"]
            )
            self.record(f"SysEx SET_PIN_CONFIG {name}", ok)

        resp = self.request_sysex(link, sx.build_get_pin_config(PIN_ANALOG_IN), "pin_config")
        ok = bool(resp and resp["pin"] == PIN_ANALOG_IN and resp["note_or_cc"] == CC_ANALOG_IN)
        self.record(f"SysEx GET_PIN_CONFIG pin {PIN_ANALOG_IN}", ok)

        items = self.collect_stream(link, sx.build_get_all_pin_configs(), "pin_config_stream_end")
        pins = sorted(i["pin"] for i in items or [] if i.get("cmd") == "pin_config")
        want = sorted(args["pin"] for _, args in PIN_CONFIGS)
        self.record("SysEx GET_ALL_PIN_CONFIGS", pins == want, f"pins {pins}")

    def _sysex_errors(self, link: MidiLink) -> None:
        self.check_error(
            link, "unknown command", sx.build_raw(UNUSED_COMMAND), UNUSED_COMMAND, sx.ERR_UNKNOWN_COMMAND
        )
        self.check_error(
            link,
            "wrong protocol major",
            sx.build_raw(sx.CMD_GET_VERSION, major=sx.PROTOCOL_MAJOR + 1),
            sx.CMD_GET_VERSION,
            sx.ERR_BAD_VERSION,
        )
        self.check_error(
            link, "unconfigured pin", sx.build_get_pin_config(UNUSED_PIN), sx.CMD_GET_PIN_CONFIG, sx.ERR_PIN_NOT_FOUND
        )
        self.check_error(
            link, "short MAC", sx.build_raw(sx.CMD_ADD_PEER, [1, 2, 3]), sx.CMD_ADD_PEER, sx.ERR_DECODE_FAILED
        )

    def _pair_with_dongle(self, link: MidiLink) -> None:
        """Store the dongle as a peer (persisted on the client for Phase 2)."""
        if not self.dongle_mac:
            self.record("SysEx ADD_PEER (dongle)", False, "no dongle MAC provided")
            return

        mac_text = sx.mac_to_string(self.dongle_mac)
        resp = self.request_sysex(link, sx.build_add_peer(self.dongle_mac), "add_peer_ok")
        self.record("SysEx ADD_PEER (dongle)", bool(resp), mac_text)

        self.check_error(
            link, "duplicate ADD_PEER", sx.build_add_peer(self.dongle_mac), sx.CMD_ADD_PEER, sx.ERR_PEER_ALREADY_EXISTS
        )

        resp = self.request_sysex(link, sx.build_get_peer(0), "peer_entry")
        self.record("SysEx GET_PEER 0", bool(resp and resp["mac"] == self.dongle_mac), mac_text)

        items = self.collect_stream(link, sx.build_get_all_peers(), "peer_stream_end")
        macs = [i["mac"] for i in items or [] if i.get("cmd") == "peer_entry"]
        self.record("SysEx GET_ALL_PEERS", self.dongle_mac in macs, f"{len(macs)} peer(s)")

        items = self.collect_stream(link, sx.build_get_config(), "get_config_ok")
        kinds = {i.get("cmd") for i in items or []}
        pins = sum(1 for i in items or [] if i.get("cmd") == "pin_config")
        ok = (
            items is not None
            and pins == len(PIN_CONFIGS)
            and {"peer_entry", "midi_loopback", "power_save", "name"} <= kinds
        )
        self.record("SysEx GET_CONFIG stream", ok, f"{pins} pins, {sorted(kinds)}")

    # --- phase 2: ESP-NOW ----------------------------------------------------
    def phase2(self, link: MidiLink) -> None:
        self.section("Phase 2 - Client via ESP-NOW (through dongle)")
        print("  (SysEx configuration is not bridged by the dongle, so it runs in Phase 1 only)")

        # The first echo waits for a couple of heartbeat (ANNOUNCE) cycles, so the dongle
        # can auto-discover the client before host -> client routing is used.
        self._echo(link, tc.channel_voice_cases(), "ESP-NOW", first_timeout=12.0)
        self._no_echo_on_io_channel(link, "ESP-NOW")
        self._echo(link, tc.system_cases(), "ESP-NOW")
        self._burst_test(link, "ESP-NOW", spacing=0.005, max_loss=0.05)
        self._api_sends(link, "ESP-NOW")
        self._io_tests(link, "ESP-NOW")

    # --- echo ------------------------------------------------------------------
    def _echo(
        self,
        link: MidiLink,
        echo_cases: list[tc.MidiCase],
        transport: str,
        timeout: float = 3.0,
        first_timeout: float | None = None,
    ) -> None:
        for i, case in enumerate(echo_cases):
            link.flush()
            self.send(link, case.type, case.message_kwargs())
            wait = first_timeout if i == 0 and first_timeout else timeout
            got = link.wait_for(case.matches, timeout=wait)
            self.record(f"Echo {case.name} ({transport})", got is not None)
            # Release notes so later tests start clean.
            if case.type == "note_on":
                self.send(link, "note_off", {"channel": case.channel - 1, "note": case.fields["note"], "velocity": 0})

    def _no_echo_on_io_channel(self, link: MidiLink, transport: str) -> None:
        """Traffic on IO_CHANNEL must not be mirrored back."""
        link.flush()
        self.send(link, "note_on", {"channel": IO_CHANNEL - 1, "note": 99, "velocity": 1})
        stray = link.wait_for(lambda m: m.type == "note_on" and m.note == 99, timeout=0.5)
        self.record(f"No echo on ch {IO_CHANNEL} ({transport})", stray is None)
        self.send(link, "note_off", {"channel": IO_CHANNEL - 1, "note": 99, "velocity": 0})

    def _burst_test(self, link: MidiLink, transport: str, spacing: float, max_loss: float) -> None:
        """Send notes back to back and check how many echoes return, and how fast."""
        if self.burst <= 0:
            return
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

        link.flush()
        for note in tc.burst_notes(self.burst):
            sent[note] = time.monotonic()
            self.send(link, "note_on", {"channel": channel, "note": note, "velocity": BURST_VELOCITY})
            link.wait_for(lambda m: False, timeout=spacing, on_message=on_message)
        if len(received) < len(sent):
            link.wait_for(lambda m: len(received) == len(sent), timeout=3.0, on_message=on_message)

        for note in sent:
            self.send(link, "note_off", {"channel": channel, "note": note, "velocity": 0})
        self.drain(link, 0.5)

        stats = tc.burst_stats(sent, received)
        detail = f"{stats['received']}/{stats['sent']} echoed"
        if stats["avg_ms"] is not None:
            detail += f", round trip avg {stats['avg_ms']:.1f} ms, p95 {stats['p95_ms']:.1f} ms, max {stats['max_ms']:.1f} ms"
        self.record(f"Burst of {self.burst} notes ({transport})", stats["loss"] <= max_loss, detail)

    # --- API sends -------------------------------------------------------------
    def _api_sends(self, link: MidiLink, transport: str) -> None:
        """Have the client call each enomik::Client send function and check what arrives."""
        for index, case in enumerate(tc.API_CASES):
            self._expect_after(link, f"API {case.name} ({transport})", tc.api_trigger(index), case.matches)
        self.drain(link, 0.3)

    # --- inputs ----------------------------------------------------------------
    def _io_tests(self, link: MidiLink, transport: str) -> None:
        if self.io_mode in ("virtual", "both"):
            self._virtual_io(link, transport)
        if self.io_mode in ("physical", "both"):
            self.send(link, *tc.fake_clear())  # read the real pins again
            self._digital_cc_test(link, PIN_DIGITAL_A, CC_DIGITAL_A, transport=transport)
            self._digital_cc_test(link, PIN_DIGITAL_B, CC_DIGITAL_B, transport=transport)
            self._analog_cc_test(link, PIN_ANALOG_IN, CC_ANALOG_IN, transport=transport)

    def _prime_fake_inputs(self, link: MidiLink) -> None:
        """Release fake buttons and set fake analog inputs to 0, then drop what they sent."""
        for pin in (PIN_DIGITAL_A, PIN_DIGITAL_B, PIN_NOTE):
            self.send(link, *tc.fake_release(pin))
        for pin in (PIN_ANALOG_IN, PIN_BEND):
            self.send(link, *tc.fake_analog(pin, 0))
        self.drain(link, 0.5)

    def _expect_after(self, link: MidiLink, name: str, fake: tuple[str, dict], predicate, timeout: float = 3.0) -> bool:
        link.flush()
        self.send(link, *fake)
        return self.record(name, link.wait_for(predicate, timeout=timeout) is not None)

    def _virtual_io(self, link: MidiLink, transport: str) -> None:
        self._prime_fake_inputs(link)

        for pin, cc in ((PIN_DIGITAL_A, CC_DIGITAL_A), (PIN_DIGITAL_B, CC_DIGITAL_B)):
            # INPUT_PULLUP is active low: press sends max (127), release min (0).
            self._expect_after(
                link, f"Fake press pin {pin} -> CC {cc} = 127 ({transport})",
                tc.fake_press(pin), tc.io_cc(cc, 127).matches,
            )
            self._expect_after(
                link, f"Fake release pin {pin} -> CC {cc} = 0 ({transport})",
                tc.fake_release(pin), tc.io_cc(cc, 0).matches,
            )

        note_on = tc.MidiCase("note", "note_on", {"note": NOTE_PIN_NOTE, "velocity": 127}, channel=IO_CHANNEL)
        self._expect_after(
            link, f"Fake press pin {PIN_NOTE} -> note on {NOTE_PIN_NOTE} ({transport})",
            tc.fake_press(PIN_NOTE), note_on.matches,
        )
        self._expect_after(
            link, f"Fake release pin {PIN_NOTE} -> note off {NOTE_PIN_NOTE} ({transport})",
            tc.fake_release(PIN_NOTE), lambda m: tc.is_note_release(m, NOTE_PIN_NOTE),
        )

        for level in tc.ANALOG_SWEEP:
            self._expect_after(
                link, f"Fake analog pin {PIN_ANALOG_IN} -> CC {CC_ANALOG_IN} = {level} ({transport})",
                tc.fake_analog(PIN_ANALOG_IN, level), tc.io_cc(CC_ANALOG_IN, level).matches,
            )

        for level, pitch, tolerance in tc.BEND_SWEEP:
            self._expect_after(
                link, f"Fake analog pin {PIN_BEND} -> pitch bend {pitch} ({transport})",
                tc.fake_analog(PIN_BEND, level), tc.io_bend(pitch, tolerance).matches,
            )

    def _digital_cc_test(
        self, link: MidiLink, pin: int, cc: int, transport: str
    ) -> bool:
        print(f"\n  Press the button on pin {pin}  [Esc = skip]")
        mido_ch = IO_CHANNEL - 1
        link.flush()

        def is_press(msg: mido.Message) -> bool:
            # INPUT_PULLUP active-low → press sends max_midi_value (127).
            return (
                msg.type == "control_change"
                and msg.channel == mido_ch
                and msg.control == cc
                and msg.value > 0
            )

        got = link.wait_for(is_press, timeout=30.0, allow_esc=True)
        if got is CANCELLED:
            return self.record(
                f"Digital pin {pin} -> CC {cc} ({transport})",
                False,
                "skipped",
            )
        return self.record(
            f"Digital pin {pin} -> CC {cc} ({transport})", got is not None
        )

    def _analog_cc_test(
        self, link: MidiLink, pin: int, cc: int, transport: str
    ) -> bool:
        """Require exact endpoints and midpoint: 0, then 64, then 127."""
        mido_ch = IO_CHANNEL - 1
        steps = (
            ("left (0)", 0),
            ("center (64)", 64),
            ("right (127)", 127),
        )
        all_ok = True

        def log_cc(msg: mido.Message) -> None:
            if (
                msg.type == "control_change"
                and msg.channel == mido_ch
                and msg.control == cc
            ):
                print(f"    received CC {cc} value={msg.value}")

        for label, want in steps:
            print(
                f"\n  Turn the potentiometer on pin {pin} to {label}  "
                f"[Esc = skip]"
            )
            link.flush()

            def is_exact(msg: mido.Message, want: int = want) -> bool:
                return (
                    msg.type == "control_change"
                    and msg.channel == mido_ch
                    and msg.control == cc
                    and msg.value == want
                )

            got = link.wait_for(
                is_exact, timeout=30.0, allow_esc=True, on_message=log_cc
            )
            name = f"Analog pin {pin} -> CC {cc} = {want} ({transport})"
            if got is CANCELLED:
                self.record(name, False, "skipped")
                all_ok = False
                continue
            if got is None:
                self.record(name, False, "timeout (no match)")
                all_ok = False
            else:
                self.record(name, True, f"value={got.value}")
        return all_ok

    # --- run -----------------------------------------------------------------
    def summary(self) -> int:
        passed = sum(1 for _, ok in self.results if ok)
        total = len(self.results)
        self.section("Summary")
        for name, ok in self.results:
            print(f"  [{PASS if ok else FAIL}] {name}")
        print(f"\nResult: {passed}/{total} passed")
        return 0 if passed == total else 1


def resolve_dongle_mac(cli_mac: str | None) -> list[int] | None:
    """MAC from --dongle-mac, or interactive prompt when omitted."""
    if cli_mac is not None:
        text = cli_mac.strip()
        if not text:
            return None
        try:
            return sx.mac_from_string(text)
        except ValueError as exc:
            print(f"Invalid --dongle-mac: {exc}", file=sys.stderr)
            sys.exit(1)

    print(
        "\nEnter the dongle's Wi-Fi STA MAC address (needed to pair for Phase 2).\n"
        "Find it via the print_mac sketch, the dongle display, or its serial log.\n"
        "Leave blank to skip pairing / Phase 2."
    )
    raw = input("Dongle MAC [xx:xx:xx:xx:xx:xx]: ").strip()
    if not raw:
        return None
    try:
        return sx.mac_from_string(raw)
    except ValueError as exc:
        print(f"  Invalid MAC: {exc}")
        return resolve_dongle_mac(None)


def main() -> int:
    parser = argparse.ArgumentParser(description="ESP-NOW-MIDI client_test wizard")
    parser.add_argument("--phase", choices=["1", "2", "both"], default="both")
    parser.add_argument(
        "--dongle-mac",
        metavar="MAC",
        help="dongle Wi-Fi STA MAC (e.g. 84:F7:03:F2:54:62); skips the MAC prompt",
    )
    parser.add_argument("--client-port", help="substring to match the client USB MIDI port")
    parser.add_argument("--dongle-port", help="substring to match the dongle USB MIDI port")
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
        print("--burst must be 0..128", file=sys.stderr)
        return 1

    if args.phase == "2" and not args.dongle_mac:
        print(
            "Phase 2 requires a dongle MAC. Pass --dongle-mac or use --phase both.",
            file=sys.stderr,
        )
        return 1

    print("=== ESP-NOW MIDI Test Wizard ===")
    print("Flash examples/client_test onto a USB-capable board (S2/S3, TinyUSB).")
    if args.io == "virtual":
        print("Inputs are faked by the firmware; no wiring needed.")
    else:
        print("Test jig wiring:")
        print(f"  - Pin {PIN_DIGITAL_A}: button to GND (INPUT_PULLUP) -> CC {CC_DIGITAL_A}")
        print(f"  - Pin {PIN_DIGITAL_B}: button to GND (INPUT_PULLUP) -> CC {CC_DIGITAL_B}")
        print(f"  - Pin {PIN_ANALOG_IN}: potentiometer (analog in) -> CC {CC_ANALOG_IN}")
        print(
            f"  - Pin {PIN_ANALOG_OUT}: PWM out <- CC {CC_ANALOG_OUT} "
            f"(loopback from pin {PIN_DIGITAL_B})"
        )

    wiz = Wizard(io_mode=args.io, burst=args.burst)
    wiz.dongle_mac = resolve_dongle_mac(args.dongle_mac)

    if args.phase in ("1", "both"):
        wiz.prompt("Connect the CLIENT board via USB")
        client_needles = [args.client_port] if args.client_port else [
            "enomik3000_client",
            "client",
            "enomik",
        ]
        try:
            link = find_link(client_needles, "client")
        except RuntimeError as exc:
            print(f"  [{FAIL}] {exc}")
            return 1
        print(f"  Using client port: {link.name}")
        try:
            wiz.phase1(link)
        finally:
            link.close()

    if args.phase in ("2", "both"):
        if wiz.dongle_mac is None and args.phase == "both":
            print("\nSkipping Phase 2 (no dongle MAC provided).")
        else:
            wiz.prompt("Connect the DONGLE via USB and keep the client powered")
            dongle_needles = [args.dongle_port] if args.dongle_port else [
                "enomik3000_dongle",
                "dongle",
                "enomik",
            ]
            try:
                link = find_link(dongle_needles, "dongle")
            except RuntimeError as exc:
                print(f"  [{FAIL}] {exc}")
                return 1
            print(f"  Using dongle port: {link.name}")
            try:
                wiz.phase2(link)
            finally:
                link.close()

    return wiz.summary()


if __name__ == "__main__":
    sys.exit(main())
