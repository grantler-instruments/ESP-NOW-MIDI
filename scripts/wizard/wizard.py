#!/usr/bin/env python3
"""Interactive test wizard for the ESP-NOW-MIDI client_test firmware.

Run via the isolated launcher (recommended — deps stay in scripts/wizard/.venv):

    ./run.sh

Do not run wizard.py directly unless that venv is already active.

Phase 1 - the client board connected directly via USB MIDI
Phase 2 - the client reached wirelessly through the dongle

Each step prints an instruction, performs an automatic check, and reports
PASS / FAIL. The process exits non-zero if any step fails.
"""

from __future__ import annotations

import argparse
import sys
from collections.abc import Callable

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
from midi_io import CANCELLED, MidiLink, find_link

TEST_CHANNEL = 10  # enomik (1-based) channel used for echo
IO_CHANNEL = 1
PIN_DIGITAL_A, CC_DIGITAL_A = 16, 16
PIN_DIGITAL_B, CC_DIGITAL_B = 17, 17
PIN_ANALOG_IN, CC_ANALOG_IN = 10, 10
PIN_ANALOG_OUT, CC_ANALOG_OUT = 21, 17  # listens to CC 17 (loopback from pin 17)
ECHO_NOTE = 64

PASS = "PASS"
FAIL = "FAIL"


class Wizard:
    def __init__(self) -> None:
        self.results: list[tuple[str, bool]] = []
        self.dongle_mac: list[int] | None = None

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

    # --- sysex helpers -------------------------------------------------------
    @staticmethod
    def send_sysex(link: MidiLink, data: list[int]) -> None:
        link.send(mido.Message("sysex", data=data))

    def request_sysex(self, link: MidiLink, data: list[int], want_cmd: str, timeout: float = 2.0):
        link.flush()
        self.send_sysex(link, data)

        def is_match(msg: mido.Message) -> bool:
            if msg.type != "sysex":
                return False
            parsed = sx.parse(list(msg.data))
            return bool(parsed and parsed.get("cmd") == want_cmd)

        msg = link.wait_for(is_match, timeout=timeout)
        return sx.parse(list(msg.data)) if msg else None

    # --- phase 1: USB --------------------------------------------------------
    def phase1(self, link: MidiLink) -> None:
        self.section("Phase 1 - Client via USB MIDI")

        # Clean slate so the run is reproducible.
        self.request_sysex(link, sx.build_reset(), "reset", timeout=2.0)

        # GET_MAC proves SysEx works in both directions and gives us the MAC.
        mac_resp = self.request_sysex(link, sx.build_get_mac(), "get_mac", timeout=2.0)
        if mac_resp:
            self.record("SysEx GET_MAC round-trip", True, sx.mac_to_string(mac_resp["mac"]))
        else:
            self.record("SysEx GET_MAC round-trip", False, "no response")

        # MIDI loopback: outgoing Client MIDI is fed back to receive handlers
        # (so pin 17 → CC 17 drives analog out pin 21 without a host round-trip).
        resp = self.request_sysex(
            link, sx.build_set_midi_loopback(True), "midi_loopback", timeout=2.0
        )
        ok = bool(resp and resp.get("enabled") is True)
        self.record("SysEx SET_MIDI_LOOPBACK on", ok)

        # Digital buttons (INPUT_PULLUP, active low) → CC.
        for pin, cc in ((PIN_DIGITAL_A, CC_DIGITAL_A), (PIN_DIGITAL_B, CC_DIGITAL_B)):
            cfg = sx.build_set_pin_config(
                pin=pin,
                mode=sx.MODE_INPUT_PULLUP,
                channel=IO_CHANNEL,
                midi_type=sx.MIDI_CONTROL_CHANGE,
                note_or_cc=cc,
            )
            resp = self.request_sysex(link, cfg, "pin_config", timeout=2.0)
            ok = bool(resp and resp["pin"] == pin and resp["note_or_cc"] == cc)
            self.record(f"SysEx SET_PIN_CONFIG pin {pin} digital -> CC {cc}", ok)

        # Potentiometer → CC 10.
        cfg = sx.build_set_pin_config(
            pin=PIN_ANALOG_IN,
            mode=sx.MODE_ANALOG_INPUT,
            channel=IO_CHANNEL,
            midi_type=sx.MIDI_CONTROL_CHANGE,
            note_or_cc=CC_ANALOG_IN,
        )
        resp = self.request_sysex(link, cfg, "pin_config", timeout=2.0)
        ok = bool(
            resp
            and resp["pin"] == PIN_ANALOG_IN
            and resp["note_or_cc"] == CC_ANALOG_IN
        )
        self.record(
            f"SysEx SET_PIN_CONFIG pin {PIN_ANALOG_IN} analog in -> CC {CC_ANALOG_IN}",
            ok,
        )

        # PWM out listens to CC 17 (same CC as digital pin 17; driven via loopback).
        cfg = sx.build_set_pin_config(
            pin=PIN_ANALOG_OUT,
            mode=sx.MODE_ANALOG_OUTPUT,
            channel=IO_CHANNEL,
            midi_type=sx.MIDI_CONTROL_CHANGE,
            note_or_cc=CC_ANALOG_OUT,
        )
        resp = self.request_sysex(link, cfg, "pin_config", timeout=2.0)
        ok = bool(
            resp
            and resp["pin"] == PIN_ANALOG_OUT
            and resp["note_or_cc"] == CC_ANALOG_OUT
        )
        self.record(
            f"SysEx SET_PIN_CONFIG pin {PIN_ANALOG_OUT} analog out <- CC {CC_ANALOG_OUT}",
            ok,
        )

        # Echo tests on the dedicated channel (multiple message types).
        self._run_echo_suite(link, transport="USB")

        # Pair with the dongle for Phase 2 (persisted on the client).
        if self.dongle_mac:
            resp = self.request_sysex(link, sx.build_add_peer(self.dongle_mac), "add_peer_ok", timeout=2.0)
            ok = bool(resp)
            self.record("SysEx ADD_PEER (dongle)", ok, sx.mac_to_string(self.dongle_mac))
        else:
            self.record("SysEx ADD_PEER (dongle)", False, "no dongle MAC provided")

        # Physical I/O.
        self._digital_cc_test(link, PIN_DIGITAL_A, CC_DIGITAL_A, transport="USB")
        self._digital_cc_test(link, PIN_DIGITAL_B, CC_DIGITAL_B, transport="USB")
        self._analog_cc_test(link, PIN_ANALOG_IN, CC_ANALOG_IN, transport="USB")

    # --- phase 2: ESP-NOW ----------------------------------------------------
    def phase2(self, link: MidiLink) -> None:
        self.section("Phase 2 - Client via ESP-NOW (through dongle)")
        print("  (the dongle bridges channel MIDI only - SysEx is USB-only)")

        # Give the periodic handshake a couple of cycles to let the dongle
        # auto-discover the client before we rely on host -> client routing.
        self._run_echo_suite(link, transport="ESP-NOW", timeout=12.0)

        self._digital_cc_test(link, PIN_DIGITAL_A, CC_DIGITAL_A, transport="ESP-NOW")
        self._digital_cc_test(link, PIN_DIGITAL_B, CC_DIGITAL_B, transport="ESP-NOW")
        self._analog_cc_test(link, PIN_ANALOG_IN, CC_ANALOG_IN, transport="ESP-NOW")

    # --- shared checks -------------------------------------------------------
    def _run_echo_suite(self, link: MidiLink, transport: str, timeout: float = 3.0) -> None:
        """Run channel-voice echo checks on TEST_CHANNEL only."""
        mido_ch = TEST_CHANNEL - 1
        link.flush()

        cases: list[tuple[str, mido.Message, Callable[[mido.Message], bool]]] = [
            (
                "note on/off",
                mido.Message("note_on", channel=mido_ch, note=ECHO_NOTE, velocity=100),
                lambda m: (
                    m.type == "note_on"
                    and m.channel == mido_ch
                    and m.note == ECHO_NOTE
                    and m.velocity == 100
                ),
            ),
            (
                "control change",
                mido.Message("control_change", channel=mido_ch, control=7, value=42),
                lambda m: (
                    m.type == "control_change"
                    and m.channel == mido_ch
                    and m.control == 7
                    and m.value == 42
                ),
            ),
            (
                "program change",
                mido.Message("program_change", channel=mido_ch, program=5),
                lambda m: (
                    m.type == "program_change"
                    and m.channel == mido_ch
                    and m.program == 5
                ),
            ),
            (
                "pitch bend center",
                mido.Message("pitchwheel", channel=mido_ch, pitch=0),
                lambda m: (
                    m.type == "pitchwheel"
                    and m.channel == mido_ch
                    and abs(m.pitch) <= 1
                ),
            ),
            (
                "pitch bend up",
                mido.Message("pitchwheel", channel=mido_ch, pitch=1000),
                lambda m: (
                    m.type == "pitchwheel"
                    and m.channel == mido_ch
                    and abs(m.pitch - 1000) <= 1
                ),
            ),
            (
                "pitch bend down",
                mido.Message("pitchwheel", channel=mido_ch, pitch=-1000),
                lambda m: (
                    m.type == "pitchwheel"
                    and m.channel == mido_ch
                    and abs(m.pitch + 1000) <= 1
                ),
            ),
            (
                "channel aftertouch",
                mido.Message("aftertouch", channel=mido_ch, value=64),
                lambda m: (
                    m.type == "aftertouch"
                    and m.channel == mido_ch
                    and m.value == 64
                ),
            ),
            (
                "poly aftertouch",
                mido.Message(
                    "polytouch", channel=mido_ch, note=ECHO_NOTE, value=80
                ),
                lambda m: (
                    m.type == "polytouch"
                    and m.channel == mido_ch
                    and m.note == ECHO_NOTE
                    and m.value == 80
                ),
            ),
        ]

        for name, outgoing, predicate in cases:
            link.flush()
            link.send(outgoing)
            got = link.wait_for(predicate, timeout=timeout)
            self.record(
                f"Echo {name} ch {TEST_CHANNEL} ({transport})",
                got is not None,
            )
            # Release note-based messages so later tests start clean.
            if outgoing.type == "note_on":
                link.send(
                    mido.Message(
                        "note_off", channel=mido_ch, note=ECHO_NOTE, velocity=0
                    )
                )

        # Non-echo channel: traffic on IO_CHANNEL must not be mirrored back.
        link.flush()
        link.send(
            mido.Message("note_on", channel=IO_CHANNEL - 1, note=99, velocity=1)
        )
        stray = link.wait_for(
            lambda m: m.type == "note_on" and m.note == 99,
            timeout=0.5,
        )
        self.record(
            f"No echo on ch {IO_CHANNEL} ({transport})",
            stray is None,
        )
        link.send(
            mido.Message(
                "note_off", channel=IO_CHANNEL - 1, note=99, velocity=0
            )
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
    args = parser.parse_args()

    if args.phase == "2" and not args.dongle_mac:
        print(
            "Phase 2 requires a dongle MAC. Pass --dongle-mac or use --phase both.",
            file=sys.stderr,
        )
        return 1

    print("=== ESP-NOW MIDI Test Wizard ===")
    print("Flash examples/client_test onto a USB-capable board (S2/S3, TinyUSB).")
    print("Test jig wiring:")
    print(f"  - Pin {PIN_DIGITAL_A}: button to GND (INPUT_PULLUP) -> CC {CC_DIGITAL_A}")
    print(f"  - Pin {PIN_DIGITAL_B}: button to GND (INPUT_PULLUP) -> CC {CC_DIGITAL_B}")
    print(f"  - Pin {PIN_ANALOG_IN}: potentiometer (analog in) -> CC {CC_ANALOG_IN}")
    print(
        f"  - Pin {PIN_ANALOG_OUT}: PWM out <- CC {CC_ANALOG_OUT} "
        f"(loopback from pin {PIN_DIGITAL_B})"
    )

    wiz = Wizard()
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
