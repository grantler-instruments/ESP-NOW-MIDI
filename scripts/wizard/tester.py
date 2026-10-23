"""Board tester logic, ported from the enomik app's tester page (#/tester).

No MIDI dependency: SysEx requests go through a `request(data, accept, timeout)`
callable, incoming MIDI arrives as raw bytes. wizard.py drives it with mido.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from typing import Callable

import enomik_sysex as sx

# Highest GPIO on the ESP32-S2/S3.
PIN_MAX = 48

# Button pins, wired to GND. Note/CC number = pin, channel 1.
BUTTON_PINS = [16, 17]
IO_CHANNEL = 1

# Onboard LED (GPIO 15 on the Lolin S2 mini). Lit by note on <pin>.
DEFAULT_LED_PIN = 15

# Touch pads (touch-capable GPIOs on the ESP32-S2/S3). Send CC <pin>.
DEFAULT_TOUCH_PINS = [1, 2]
# On at this change from the untouched reading, in percent.
TOUCH_THRESHOLD = 20

# Steps through every type a pin can send, one per press.
CYCLE_PIN = 16
# Every MIDI type the firmware sends from an input pin.
MIDI_TYPES = [
    (sx.MIDI_NOTE_ON, "Note on"),
    (sx.MIDI_NOTE_OFF, "Note off"),
    (sx.MIDI_CONTROL_CHANGE, "Control change"),
    (sx.MIDI_PITCH_BEND, "Pitch bend"),
]

# The LED follows this button through MIDI loopback in the default config.
DEFAULT_LED_SOURCE = 16

Bytes = list[int]
Version = tuple[int, ...]
Request = Callable[[Bytes, Callable[[dict], bool], float], "dict | None"]


def _pin_config(pin: int, mode: int, threshold: int, status: int, number: int) -> Bytes:
    """SET_PIN_CONFIG on channel 1, MIDI values 0-127 (inner data, no F0/F7)."""
    return sx.build_set_pin_config(
        pin=pin, mode=mode, channel=IO_CHANNEL, midi_type=status, note_or_cc=number, threshold=threshold
    )


def button_config(pin: int, status: int = sx.MIDI_CONTROL_CHANGE, number: int | None = None) -> Bytes:
    """Input pull-up sending <status> with note/CC <number> (default: the pin):
    max value while pressed, min value when released."""
    return _pin_config(pin, sx.MODE_INPUT_PULLUP, 0, status, pin if number is None else number)


def touch_config(pin: int) -> Bytes:
    """Touch pad in threshold mode: CC <pin> 127 on touch, 0 on release."""
    return _pin_config(pin, sx.MODE_INPUT_TOUCH, TOUCH_THRESHOLD, sx.MIDI_CONTROL_CHANGE, pin)


def led_config(pin: int, status: int = sx.MIDI_NOTE_ON, number: int | None = None) -> Bytes:
    """Output switched by note on/off <number> (default: the pin), or by a CC."""
    return _pin_config(pin, sx.MODE_OUTPUT, 0, status, pin if number is None else number)


def parse_reply(data: Bytes) -> dict | None:
    """Parse the inner data of an enomik SysEx reply, None if not one we use."""
    if len(data) < 4 or data[0] != sx.MANUFACTURER_ID:
        return None
    cmd = data[3]
    p = list(data[4:])
    if cmd == sx.RESP_SET_PIN_CONFIG and len(p) >= 8:
        return {"cmd": "pin_config", "pin": p[0]}
    if cmd == sx.RESP_ADD_PEER and p[:1] == [1]:
        return {"cmd": "add_peer_ok"}
    if cmd == sx.RESP_RESET:
        return {"cmd": "reset_ok"}
    if cmd == sx.RESP_SET_MIDI_LOOPBACK and len(p) >= 1:
        return {"cmd": "midi_loopback", "enabled": p[0] != 0}
    # Before 0.13 the MAC reply echoes the request command (0x06)
    if cmd in (sx.RESP_GET_MAC, sx.CMD_GET_MAC) and len(p) >= 12 and all(n < 16 for n in p[:12]):
        mac = [(p[2 * i] << 4) | p[2 * i + 1] for i in range(6)]
        return {"cmd": "mac", "mac": mac, "header": (data[1], data[2])}
    # [major, minor] or, since 0.19, [major, minor, patch]
    if cmd == sx.RESP_GET_VERSION and len(p) >= 2:
        return {"cmd": "version", "version": tuple(p[:3])}
    if cmd == sx.RESP_SET_RAW_STREAM and len(p) >= 3:
        return {"cmd": "raw_stream", "enabled": p[1] != 0}
    if cmd == sx.CMD_RAW_VALUE:
        return sx.parse(data)
    if cmd == sx.RESP_ERROR and len(p) >= 2:
        return {"cmd": "error", "failed_request": p[0], "error_code": p[1]}
    return None


def button_event(data: Bytes, touch_pins: list[int] | tuple[int, ...] = ()) -> tuple[int, int, bool] | None:
    """(pin, status, down) for a button press or release in this message, or None."""
    if len(data) < 3 or (data[0] & 0x0F) != IO_CHANNEL - 1:
        return None
    status = data[0] & 0xF0
    # Pitch bend carries no pin; only the cycling pin sends it.
    if status == sx.MIDI_PITCH_BEND:
        return CYCLE_PIN, status, (data[1] | (data[2] << 7)) > 8192
    if status not in (sx.MIDI_NOTE_ON, sx.MIDI_NOTE_OFF, sx.MIDI_CONTROL_CHANGE):
        return None
    if data[1] not in BUTTON_PINS and data[1] not in touch_pins:
        return None
    return data[1], status, data[2] > 0


def note_down(data: Bytes, note: int) -> bool | None:
    """Note on/off <note> on channel 1: pressed (True), released (False), or None."""
    if len(data) < 3 or data[1] != note:
        return None
    if data[0] == sx.MIDI_NOTE_ON | (IO_CHANNEL - 1):
        return data[2] > 0
    if data[0] == sx.MIDI_NOTE_OFF | (IO_CHANNEL - 1):
        return False
    return None


def parse_pins(text: str) -> list[int]:
    """Comma or space separated pin numbers; ValueError on anything else."""
    return [int(part) for part in re.split(r"[\s,]+", text) if part]


def pin_error(pin: int) -> str:
    """Validation message for a pin number, "" when valid."""
    return "" if 0 <= pin <= PIN_MAX else f"0 to {PIN_MAX}"


def settings_errors(led_pin: int, touch_pins: list[int]) -> list[str]:
    """Problems with the LED and touch pins, empty when valid."""
    errors = []
    led = pin_error(led_pin) or ("used by a button" if led_pin in BUTTON_PINS else "")
    if led:
        errors.append(f"LED pin: {led}")
    bad = next((pin for pin in touch_pins if pin_error(pin)), None)
    if bad is not None:
        errors.append(f"touch pins: {pin_error(bad)}, comma separated")
    elif any(pin in BUTTON_PINS or pin == led_pin or touch_pins.index(pin) != i for i, pin in enumerate(touch_pins)):
        errors.append("touch pins: used twice or by a button/LED")
    return errors


def version_to_string(version: Version) -> str:
    return ".".join(str(part) for part in version)


def supports_live_values(version: Version | None) -> bool:
    """RAW_STREAM (live touch readings) since firmware 0.19."""
    return version is not None and len(version) >= 2 and version[0] == 0 and version[1] >= 19


# --- Requests ----------------------------------------------------------------
def configure_pin(request: Request, pin: int, data: Bytes) -> bool:
    """Send a SET_PIN_CONFIG built by *_config(pin); True when acknowledged."""
    reply = request(data, lambda r: r["cmd"] == "pin_config" and r["pin"] == pin, 2.0)
    return reply is not None and reply["cmd"] == "pin_config"


def add_peer(request: Request, mac: list[int]) -> bool:
    """Add a peer; one that already exists counts as success."""
    reply = request(sx.build_add_peer(mac), lambda r: r["cmd"] == "add_peer_ok", 2.0)
    return reply is not None and (
        reply["cmd"] == "add_peer_ok"
        or (reply["cmd"] == "error" and reply["error_code"] == sx.ERR_PEER_ALREADY_EXISTS)
    )


def set_midi_loopback(request: Request, on: bool) -> bool:
    reply = request(sx.build_set_midi_loopback(on), lambda r: r["cmd"] == "midi_loopback", 2.0)
    return reply is not None and reply["cmd"] == "midi_loopback" and reply["enabled"] == on


@dataclass
class BoardInfo:
    mac: list[int] | None
    version: Version | None

    def text(self) -> str:
        """'firmware 0.19.0 · MAC 84:F7:…', 'unknown' for what is missing."""
        firmware = version_to_string(self.version) if self.version else "unknown"
        mac = sx.mac_to_string(self.mac) if self.mac else "unknown"
        return f"firmware {firmware} · MAC {mac}"


def read_board_info(request: Request) -> BoardInfo:
    """MAC and firmware version of a board. A board before 0.13 may not answer
    GET_VERSION; its MAC reply header still carries major/minor. Dongles before
    0.19 answer neither."""
    mac_reply = request(sx.build_get_mac(), lambda r: r["cmd"] == "mac", 2.0)
    mac = mac_reply if mac_reply is not None and mac_reply["cmd"] == "mac" else None
    version_reply = request(sx.build_get_version(), lambda r: r["cmd"] == "version", 1.0 if mac else 2.0)
    if version_reply is not None and version_reply["cmd"] == "version":
        version = version_reply["version"]
    else:
        version = mac["header"] if mac else None
    return BoardInfo(mac=mac["mac"] if mac else None, version=version)


def write_default_config(request: Request, led_pin: int, dongle_mac: list[int] | None) -> list[tuple[str, bool]]:
    """Config a tested board ships with: RESET, the buttons send CC <pin>, the LED
    follows CC 16 through MIDI loopback, and the tested dongle stays a peer.
    One (text, ok) line per request, ok when acknowledged."""
    lines = []
    reset = request(sx.build_reset(), lambda r: r["cmd"] == "reset_ok", 2.0)
    lines.append(("reset: no pins, no peers", reset is not None and reset["cmd"] == "reset_ok"))
    for pin in BUTTON_PINS:
        lines.append((f"pin {pin}: input pull-up → CC {pin}", configure_pin(request, pin, button_config(pin))))
    lines.append(
        (
            f"pin {led_pin}: output ← CC {DEFAULT_LED_SOURCE} (LED)",
            configure_pin(request, led_pin, led_config(led_pin, sx.MIDI_CONTROL_CHANGE, DEFAULT_LED_SOURCE)),
        )
    )
    lines.append(("MIDI loopback on", set_midi_loopback(request, True)))
    if dongle_mac:
        lines.append((f"add dongle {sx.mac_to_string(dongle_mac)} as peer", add_peer(request, dongle_mac)))
    return lines


# --- Test state --------------------------------------------------------------
# How a message reached the tester: from the client directly, or the dongle.
PATHS = ("usb", "wireless")

# Pin 16 gets one row per MIDI type; the other buttons stay on CC.
ROWS = [
    (pin, status, label)
    for pin in BUTTON_PINS
    for status, label in MIDI_TYPES
    if pin == CYCLE_PIN or status == sx.MIDI_CONTROL_CHANGE
]


@dataclass
class LoopNote:
    usb: bool = False
    wireless: bool = False
    held: bool = False
    released: bool = False


@dataclass
class TesterState:
    """Test results; every incoming MIDI message goes through midi()."""

    led_pin: int = DEFAULT_LED_PIN
    touch_pins: list[int] = field(default_factory=lambda: list(DEFAULT_TOUCH_PINS))
    # Per button row (pin, status): paths a press arrived on.
    buttons: dict[tuple[int, int], set[str]] = field(
        default_factory=lambda: {(pin, status): set() for pin, status, _ in ROWS}
    )
    # Buttons held right now (USB).
    pressed: dict[int, bool] = field(default_factory=dict)
    # Index into MIDI_TYPES that pin 16 sends now.
    type_index: int = 0
    # Set by a press of the current type. A fresh pin config reports the
    # released state right away, which must not count as a release.
    armed: bool = False
    touch: dict[int, set[str]] = field(default_factory=dict)
    # Pin 16 sends note <LED pin> and the client handles its own MIDI.
    loopback_on: bool = False
    loop_note: LoopNote = field(default_factory=LoopNote)
    # The user's answer: did the LED light?
    lit: bool | None = None

    def midi(self, path: str, data: Bytes) -> None:
        if self.loopback_on:
            self._loopback_note(path, data)
        else:
            self._pin_message(path, data)

    def set_loopback(self, on: bool) -> None:
        # Either way pin 16 gets a fresh config, which reports a release.
        self.loopback_on = on
        self.armed = False
        if on:
            self.loop_note = LoopNote()
            self.lit = None

    def cycle_config(self) -> tuple[Bytes, str]:
        """What pin 16 should send now: the current type, or the LED note during loopback."""
        if self.loopback_on:
            return button_config(CYCLE_PIN, sx.MIDI_NOTE_ON, self.led_pin), f"note {self.led_pin}"
        status, label = MIDI_TYPES[self.type_index]
        return button_config(CYCLE_PIN, status), label

    def _loopback_note(self, path: str, data: Bytes) -> None:
        down = note_down(data, self.led_pin)
        if down is None:
            return
        note = self.loop_note
        if down:
            setattr(note, path, True)
            note.held = note.held or path == "usb"
        # The fresh pin config reports a release before any press.
        elif path == "usb" and note.held:
            note.released = True

    def _pin_message(self, path: str, data: Bytes) -> None:
        ev = button_event(data, self.touch_pins)
        if ev is None:
            return
        pin, status, down = ev
        if pin in self.touch_pins:
            if down:
                self.touch.setdefault(pin, set()).add(path)
            return
        if path == "usb":
            self.pressed[pin] = down
        if down and (pin, status) in self.buttons:
            self.buttons[(pin, status)].add(path)

        current = pin == CYCLE_PIN and status == MIDI_TYPES[self.type_index][0]
        if path != "usb" or not current:
            return
        if down:
            self.armed = True
        elif self.armed:
            self.type_index = (self.type_index + 1) % len(MIDI_TYPES)
            self.armed = False


def buttons_passed(state: TesterState, paths: list[str]) -> bool:
    return all(set(paths) <= state.buttons[(pin, status)] for pin, status, _ in ROWS)


def loopback_passed(state: TesterState, paths: list[str]) -> bool:
    return state.lit is True and all(getattr(state.loop_note, p) for p in paths)


def touch_passed(state: TesterState, paths: list[str]) -> bool:
    return all(set(paths) <= state.touch.get(pin, set()) for pin in state.touch_pins)
