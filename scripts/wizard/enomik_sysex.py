"""Build and parse enomik SysEx messages.

The byte layout mirrors enomik_sysex.h. All builders return the *inner* SysEx
data (i.e. everything between the 0xF0 start and 0xF7 end bytes), which is the
form `mido.Message('sysex', data=...)` expects.

Wire layout of a standard packet:
    F0 7D MAJOR MINOR CMD <payload...> F7
so the inner data is:
    7D MAJOR MINOR CMD <payload...>
"""

from __future__ import annotations

# Keep these in sync with version.h
PROTOCOL_MAJOR = 0
PROTOCOL_MINOR = 12

MANUFACTURER_ID = 0x7D

# Commands (enomik::SysExCommand)
CMD_SET_PIN_CONFIG = 0x01
CMD_GET_PIN_CONFIG = 0x02
CMD_CLEAR_PIN_CONFIGS = 0x03
CMD_GET_ALL_PIN_CONFIGS = 0x04
CMD_DELETE_PIN_CONFIG = 0x05
CMD_GET_MAC = 0x06
CMD_ADD_PEER = 0x07
CMD_GET_PEERS = 0x08
CMD_RESET = 0x09
CMD_GET_VERSION = 0x0A

# Response codes
RESP_GET_PIN_CONFIG = 0x42
RESP_GET_ALL_PIN_CONFIGS = 0x44
RESP_ADD_PEER = 0x47
RESP_GET_PEERS = 0x48
RESP_RESET = 0x49
RESP_GET_VERSION = 0x4A

# Pin modes (enomik_io.h)
MODE_INPUT = 0x00
MODE_OUTPUT = 0x01
MODE_INPUT_PULLUP = 0x02
MODE_ANALOG_INPUT = 0x03
MODE_ANALOG_OUTPUT = 0x04
MODE_INPUT_TOUCH = 0x05

# MIDI status bytes (midiHelpers.h)
MIDI_NOTE_OFF = 0x80
MIDI_NOTE_ON = 0x90
MIDI_CONTROL_CHANGE = 0xB0


def _header(cmd: int) -> list[int]:
    return [MANUFACTURER_ID, PROTOCOL_MAJOR, PROTOCOL_MINOR, cmd]


def _mac_to_nibbles(mac: list[int]) -> list[int]:
    nibbles: list[int] = []
    for byte in mac:
        nibbles.append((byte >> 4) & 0x0F)
        nibbles.append(byte & 0x0F)
    return nibbles


def _nibbles_to_mac(nibbles: list[int]) -> list[int]:
    mac: list[int] = []
    for i in range(0, len(nibbles) - 1, 2):
        mac.append(((nibbles[i] & 0x0F) << 4) | (nibbles[i + 1] & 0x0F))
    return mac


# --- Builders ----------------------------------------------------------------
def build_get_mac() -> list[int]:
    return _header(CMD_GET_MAC)


def build_get_version() -> list[int]:
    return _header(CMD_GET_VERSION)


def build_reset() -> list[int]:
    return _header(CMD_RESET)


def build_get_peers() -> list[int]:
    return _header(CMD_GET_PEERS)


def build_add_peer(mac: list[int]) -> list[int]:
    return _header(CMD_ADD_PEER) + _mac_to_nibbles(mac)


def build_get_pin_config(pin: int) -> list[int]:
    return _header(CMD_GET_PIN_CONFIG) + [pin]


def build_set_pin_config(
    pin: int,
    mode: int,
    channel: int,
    midi_type: int,
    note_or_cc: int,
    min_value: int = 0,
    max_value: int = 127,
    threshold: int = 0,
) -> list[int]:
    # Layout decoded by SysExDecoder::decodePinConfig:
    # pin, mode, threshold, midi_channel, midi_type/2, note_or_cc, min, max
    return _header(CMD_SET_PIN_CONFIG) + [
        pin,
        mode,
        threshold,
        channel,
        midi_type // 2,
        note_or_cc,
        min_value,
        max_value,
    ]


# --- Parser ------------------------------------------------------------------
def parse(data: list[int]) -> dict | None:
    """Parse the inner data of a received SysEx message.

    `data` is the mido sysex payload (without F0/F7), e.g. starting with 0x7D.
    Returns a dict describing the response, or None if it is not an enomik
    message we recognise.
    """
    d = list(data)
    if not d or d[0] != MANUFACTURER_ID:
        return None

    # GET_PEERS response is special: it has no MAJOR/MINOR version header.
    # Layout: 7D 48 <12 nibbles per peer>. While PROTOCOL_MAJOR == 0 the second
    # byte of a standard packet is 0, so 0x48 here is unambiguous.
    if len(d) >= 2 and d[1] == RESP_GET_PEERS:
        macs = []
        nibbles = d[2:]
        for i in range(0, len(nibbles) - 11, 12):
            macs.append(_nibbles_to_mac(nibbles[i : i + 12]))
        return {"cmd": "get_peers", "peers": macs}

    if len(d) < 4:
        return None

    cmd = d[3]
    payload = d[4:]

    if cmd == CMD_GET_MAC:
        return {"cmd": "get_mac", "mac": _nibbles_to_mac(payload[:12])}

    if cmd == RESP_GET_VERSION:
        return {
            "cmd": "version",
            "major": payload[0] if len(payload) > 0 else None,
            "minor": payload[1] if len(payload) > 1 else None,
        }

    if cmd == RESP_GET_PIN_CONFIG and len(payload) >= 8:
        return {
            "cmd": "pin_config",
            "pin": payload[0],
            "mode": payload[1],
            "threshold": payload[2],
            "channel": payload[3],
            "midi_type": payload[4] * 2,
            "note_or_cc": payload[5],
            "min": payload[6],
            "max": payload[7],
        }

    if cmd == RESP_ADD_PEER and len(payload) >= 1:
        return {"cmd": "add_peer", "success": bool(payload[0])}

    if cmd == RESP_RESET:
        return {"cmd": "reset"}

    return {"cmd": "unknown", "raw": d}


def mac_to_string(mac: list[int]) -> str:
    return ":".join(f"{b:02X}" for b in mac)


def mac_from_string(text: str) -> list[int]:
    parts = text.strip().replace("-", ":").split(":")
    if len(parts) != 6:
        raise ValueError("MAC must have 6 octets, e.g. 84:F7:03:F2:54:62")
    return [int(p, 16) for p in parts]
