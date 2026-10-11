"""Wizard test cases and pure helpers (no mido import, so they can be unit tested).

Messages are described as (mido type, fields) and matched against any object
with the same attributes, e.g. a mido.Message.
"""

from __future__ import annotations

from dataclasses import dataclass, field

# Channels are 1-based, as in the library. mido uses channel - 1.
ECHO_CHANNEL = 10  # client_test echoes channel voice back on this channel
IO_CHANNEL = 1  # MIDI sent by the client's configured input pins
FAKE_CHANNEL = 15  # wizard -> client_test: drive the fake (virtual) inputs
API_TRIGGER_CHANNEL = 14  # wizard -> client_test: program change p runs API case p
API_CHANNEL = 12  # channel voice messages the client sends from API cases

# Pins configured over SysEx. Real wiring is only needed with --io physical.
PIN_DIGITAL_A, CC_DIGITAL_A = 16, 16
PIN_DIGITAL_B, CC_DIGITAL_B = 17, 17
PIN_ANALOG_IN, CC_ANALOG_IN = 10, 10
PIN_ANALOG_OUT, CC_ANALOG_OUT = 21, 17  # listens to CC 17 (loopback from pin 17)
PIN_NOTE, NOTE_PIN_NOTE = 18, 60  # digital pin -> note on/off
PIN_BEND = 11  # analog pin -> pitch bend

ECHO_NOTE = 64


@dataclass(frozen=True)
class MidiCase:
    """One message to send and the echo to expect."""

    name: str
    type: str
    fields: dict = field(default_factory=dict)
    tolerance: dict = field(default_factory=dict)  # field -> allowed abs difference
    channel: int | None = None  # 1-based; None for system messages

    def message_kwargs(self) -> dict:
        kwargs = dict(self.fields)
        if self.channel is not None:
            kwargs["channel"] = self.channel - 1
        return kwargs

    def matches(self, msg) -> bool:
        if getattr(msg, "type", None) != self.type:
            return False
        if self.channel is not None and getattr(msg, "channel", None) != self.channel - 1:
            return False
        for name, want in self.fields.items():
            got = getattr(msg, name, None)
            if got is None:
                return False
            if isinstance(want, tuple):
                if tuple(got) != want:
                    return False
            elif abs(got - want) > self.tolerance.get(name, 0):
                return False
        return True


def _voice(name: str, type_: str, **fields) -> MidiCase:
    return MidiCase(name, type_, fields, channel=ECHO_CHANNEL)


def channel_voice_cases() -> list[MidiCase]:
    """Every channel voice message type, including edge values."""
    return [
        _voice("note on", "note_on", note=ECHO_NOTE, velocity=100),
        _voice("note on velocity 127", "note_on", note=127, velocity=127),
        _voice("note on velocity 1", "note_on", note=0, velocity=1),
        _voice("note off", "note_off", note=ECHO_NOTE, velocity=64),
        _voice("control change", "control_change", control=7, value=42),
        _voice("control change 0 = 0", "control_change", control=0, value=0),
        _voice("control change 119 = 127", "control_change", control=119, value=127),
        _voice("program change", "program_change", program=5),
        _voice("program change 0", "program_change", program=0),
        _voice("program change 127", "program_change", program=127),
        _voice("pitch bend center", "pitchwheel", pitch=0),
        _voice("pitch bend +1000", "pitchwheel", pitch=1000),
        _voice("pitch bend -1000", "pitchwheel", pitch=-1000),
        _voice("pitch bend min", "pitchwheel", pitch=-8192),
        _voice("pitch bend max", "pitchwheel", pitch=8191),
        _voice("channel aftertouch", "aftertouch", value=64),
        _voice("channel aftertouch 0", "aftertouch", value=0),
        _voice("channel aftertouch 127", "aftertouch", value=127),
        _voice("poly aftertouch", "polytouch", note=ECHO_NOTE, value=80),
        _voice("poly aftertouch edges", "polytouch", note=127, value=0),
    ]


def system_cases() -> list[MidiCase]:
    """System real-time and common messages (echoed regardless of channel)."""
    return [
        MidiCase("clock", "clock"),
        MidiCase("start", "start"),
        MidiCase("continue", "continue"),
        MidiCase("stop", "stop"),
        MidiCase("song position 0", "songpos", {"pos": 0}),
        MidiCase("song position 1000", "songpos", {"pos": 1000}),
        MidiCase("song position max", "songpos", {"pos": 16383}),
        MidiCase("song select 0", "song_select", {"song": 0}),
        MidiCase("song select 127", "song_select", {"song": 127}),
    ]


# --- API sends -------------------------------------------------------------------
# client_test calls the enomik::Client send API when it receives program change p
# on API_TRIGGER_CHANNEL. API_CASES[p] is what it must send; keep the order in sync
# with runApiCase() in examples/client_test/client_test.ino.

API_SYSEX_SHORT = (0x7D, 0x7F, 0x7F, 0x01, 0x02, 0x03)
API_SYSEX_LONG = (0x7D, 0x7F, 0x7F) + tuple(range(0x10, 0x10 + 17))  # spans several USB packets
# 300 bytes with F0/F7: more than one ESP-NOW frame (240-byte payloads), so it is
# split and reassembled. The firmware builds it at run time (runApiCase()).
API_SYSEX_FRAGMENTED_LENGTH = 300
API_SYSEX_FRAGMENTED = (0x7D, 0x7F, 0x7F) + tuple(i & 0x7F for i in range(API_SYSEX_FRAGMENTED_LENGTH - 5))


def _api(name: str, type_: str, **fields) -> MidiCase:
    return MidiCase(name, type_, fields, channel=API_CHANNEL)


API_CASES = [
    _api("sendNoteOn", "note_on", note=60, velocity=100),
    _api("sendNoteOff", "note_off", note=60, velocity=0),
    _api("sendControlChange", "control_change", control=20, value=64),
    _api("sendProgramChange", "program_change", program=7),
    _api("sendPitchBend min", "pitchwheel", pitch=-8192),
    _api("sendPitchBend center", "pitchwheel", pitch=0),
    _api("sendPitchBend max", "pitchwheel", pitch=8191),
    _api("sendAfterTouch", "aftertouch", value=50),
    _api("sendPolyAfterTouch", "polytouch", note=60, value=70),
    MidiCase("sendClock", "clock"),
    MidiCase("sendStart", "start"),
    MidiCase("sendContinue", "continue"),
    MidiCase("sendStop", "stop"),
    MidiCase("sendSongPosition", "songpos", {"pos": 1234}),
    MidiCase("sendSongSelect", "song_select", {"song": 3}),
    MidiCase("sendSysEx short", "sysex", {"data": API_SYSEX_SHORT}),
    MidiCase("sendSysEx long", "sysex", {"data": API_SYSEX_LONG}),
    MidiCase("sendSysEx fragmented", "sysex", {"data": API_SYSEX_FRAGMENTED}),
]


def api_trigger(index: int) -> tuple[str, dict]:
    return "program_change", {"channel": API_TRIGGER_CHANNEL - 1, "program": index}


# --- Fake inputs ---------------------------------------------------------------
# client_test reads fake values instead of the pins when told to over FAKE_CHANNEL:
#   control change  control = pin, value = 0..127  -> analog pin reads that level
#   note on         note = pin, velocity > 0       -> digital pin pressed (LOW)
#   note off        note = pin                     -> digital pin released (HIGH)
#   program change                                 -> all pins read the hardware again


def fake_analog(pin: int, value: int) -> tuple[str, dict]:
    return "control_change", {"channel": FAKE_CHANNEL - 1, "control": pin, "value": value}


def fake_press(pin: int) -> tuple[str, dict]:
    return "note_on", {"channel": FAKE_CHANNEL - 1, "note": pin, "velocity": 127}


def fake_release(pin: int) -> tuple[str, dict]:
    return "note_off", {"channel": FAKE_CHANNEL - 1, "note": pin, "velocity": 0}


def fake_clear() -> tuple[str, dict]:
    return "program_change", {"channel": FAKE_CHANNEL - 1, "program": 0}


# Each step differs from the previous one, so the pin always sends a change.
ANALOG_SWEEP = (64, 0, 127, 32, 96)

# (fake level, expected signed pitch bend, tolerance). The middle level is not
# exactly center: 7-bit levels map to the middle of their 14-bit range.
BEND_SWEEP = ((127, 8191, 0), (64, 0, 256), (0, -8192, 0))


def io_cc(cc: int, value: int) -> MidiCase:
    return MidiCase(f"CC {cc} = {value}", "control_change", {"control": cc, "value": value}, channel=IO_CHANNEL)


def io_bend(pitch: int, tolerance: int) -> MidiCase:
    return MidiCase(f"pitch bend {pitch}", "pitchwheel", {"pitch": pitch}, {"pitch": tolerance}, channel=IO_CHANNEL)


def is_note_release(msg, note: int, channel: int = IO_CHANNEL) -> bool:
    """Note off, or note on with velocity 0."""
    if getattr(msg, "channel", None) != channel - 1 or getattr(msg, "note", None) != note:
        return False
    return msg.type == "note_off" or (msg.type == "note_on" and msg.velocity == 0)


# --- Burst ----------------------------------------------------------------------
def burst_notes(count: int) -> list[int]:
    """Distinct note numbers so each echo can be matched to its send."""
    if not 1 <= count <= 128:
        raise ValueError("burst count must be 1..128")
    return list(range(count))


def burst_stats(sent: dict[int, float], received: dict[int, float]) -> dict:
    """Loss and round-trip times (ms) from send/receive timestamps in seconds."""
    rtts = sorted((received[n] - sent[n]) * 1000.0 for n in sent if n in received)
    lost = len(sent) - len(rtts)
    stats = {
        "sent": len(sent),
        "received": len(rtts),
        "lost": lost,
        "loss": lost / len(sent) if sent else 0.0,
        "avg_ms": None,
        "p95_ms": None,
        "max_ms": None,
    }
    if rtts:
        stats["avg_ms"] = sum(rtts) / len(rtts)
        stats["p95_ms"] = rtts[min(len(rtts) - 1, int(0.95 * len(rtts)))]
        stats["max_ms"] = rtts[-1]
    return stats
