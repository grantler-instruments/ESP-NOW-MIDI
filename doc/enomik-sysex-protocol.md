# Enomik SysEx configuration protocol

The enomik configuration protocol lets a host configure an ESP-NOW MIDI client over **MIDI System Exclusive (SysEx)**. It is implemented in firmware by `enomik::Client` / `enomik::IO` and used by the [enomik web app](https://grantler-instruments.github.io/enomik-app/) and the library test wizard.

All multi-byte values in SysEx payloads are **7-bit** (0–127), as required by MIDI.

## Packet layout

Every message uses this wire format:

```
F0  7D  MAJOR  MINOR  CMD  [payload…]  F7
│   │    │      │      │
│   │    │      │      └─ command (request or response)
│   │    │      └─ protocol minor (from library version)
│   │    └─ protocol major (compatibility gate)
│   └─ manufacturer ID (non-commercial)
└─ SysEx start
```

| Field | Value | Notes |
|---|---|---|
| Start | `0xF0` | MIDI SysEx start |
| Manufacturer | `0x7D` | Non-commercial manufacturer ID |
| MAJOR | `0x00` … `0x7F` | Must match firmware major or request is rejected |
| MINOR | `0x00` … `0x7F` | Informational; not used for compatibility |
| CMD | see below | Request or response command byte |
| End | `0xF7` | MIDI SysEx end |

**Compatibility:** firmware accepts packets whose **MAJOR** equals `ESP_NOW_MIDI_VERSION_MAJOR` from [`version.h`](../version.h). Mismatch yields an error response (see [Errors](#errors)).

When building messages for Web MIDI / `mido`, the inner data is everything **between** `F0` and `F7` (i.e. starting with `7D`).

## Command numbering

| Kind | Rule | Range (current) |
|---|---|---|
| Request | assigned sequentially | `0x01` … `0x0A` |
| Success response | **request + 64** (`0x40`) | `0x41` … `0x4A` |
| Error response | fixed **`0x7F`** | always `0x7F` |

**Reserved:** request `0x3F` must not be used — its success pair would be `0x7F`, which is reserved for errors. Future requests should stay ≤ `0x3E` if the `+ 64` rule is kept.

## Commands (requests)

| CMD | Name | Payload | Success response |
|---|---|---|---|
| `0x01` | SET_PIN_CONFIG | [Pin config](#pin-config-payload) (8 bytes) | `0x41` + same pin config |
| `0x02` | GET_PIN_CONFIG | pin (1 byte) | `0x42` + pin config, or [error](#errors) |
| `0x03` | CLEAR_PIN_CONFIGS | (none) | `0x43` (empty) |
| `0x04` | GET_ALL_PIN_CONFIGS | (none) | one `0x44` + pin config per stored pin |
| `0x05` | DELETE_PIN_CONFIG | pin (1 byte) | `0x45` + pin byte, or [error](#errors) |
| `0x06` | GET_MAC | (none) | `0x46` + [MAC nibbles](#mac-address-encoding) |
| `0x07` | ADD_PEER | [MAC nibbles](#mac-address-encoding) (12 bytes) | `0x47` + success byte (`1` / `0`) |
| `0x08` | GET_PEERS | (none) | `0x48` + 12 nibbles per peer MAC |
| `0x09` | RESET | (none) | `0x49` (empty) |
| `0x0A` | GET_VERSION | (none) | `0x4A` + major + minor |

### Pin config payload

Used in SET (`0x01`) requests and in pin-config success responses (`0x41`, `0x42`, `0x44`):

| Offset | Field | Description |
|---|---|---|
| 0 | pin | GPIO pin number |
| 1 | mode | [Pin mode](#pin-modes) |
| 2 | threshold | Debounce / analog threshold (device-specific) |
| 3 | midi_channel | MIDI channel 1–16 |
| 4 | midi_type | MIDI status ÷ 2 (see [MIDI type](#midi-type-encoding)) |
| 5 | note_or_cc | Note number or CC number |
| 6 | min_midi_value | Mapped minimum (usually 0) |
| 7 | max_midi_value | Mapped maximum (usually 127) |

### Pin modes

| Value | Name |
|---|---|
| `0x00` | INPUT |
| `0x01` | OUTPUT |
| `0x02` | INPUT_PULLUP |
| `0x03` | ANALOG_INPUT |
| `0x04` | ANALOG_OUTPUT (PWM) |
| `0x05` | INPUT_TOUCH |

### MIDI type encoding

The protocol stores `midi_type / 2` in one byte because SysEx data is 7-bit.

| MIDI message | Status byte | Stored value |
|---|---|---|
| Note Off | `0x80` (128) | `0x40` (64) |
| Note On | `0x90` (144) | `0x48` (72) |
| Control Change | `0xB0` (176) | `0x58` (88) |
| Program Change | `0xC0` (192) | `0x60` (96) |

Decode: `midi_type = stored * 2`.

### MAC address encoding

MAC addresses are sent as **12 nibbles** (high nibble, low nibble per octet):

```
MAC 84:F7:03:F2:54:62  →  08 04 0F 07 00 03 0F 02 05 04 06 02
```

Used in GET_MAC / GET_PEERS responses and ADD_PEER requests.

## Success response details

### SET_PIN_CONFIG → `0x41`

Echoes the stored configuration (same 8-byte payload as request).

### GET_PIN_CONFIG → `0x42`

Returns configuration for the requested pin. If the pin is not configured, see [PIN_NOT_FOUND](#error-codes).

### GET_ALL_PIN_CONFIGS → `0x44`

Sends **one SysEx message per pin** (each with cmd `0x44`). There is no explicit “done” packet; hosts should use a timeout or count.

### DELETE_PIN_CONFIG → `0x45`

Payload: deleted pin number (1 byte).

### GET_MAC → `0x46`

Payload: 12 MAC nibbles.

### ADD_PEER → `0x47`

Payload: one byte — `1` success, `0` failure. Decode errors use [errors](#errors) instead.

### GET_PEERS → `0x48`

Payload: concatenated 12-nibble MAC blocks, one per peer. Empty list = header + `F7` only.

### RESET → `0x49`

Empty payload. Clears pin configs and invokes device reset hooks.

### GET_VERSION → `0x4A`

Payload: major (1 byte), minor (1 byte) — protocol/library version.

## Errors

Errors use a **single global command** `0x7F`, not request + 128 (invalid in 7-bit MIDI).

```
F0  7D  MAJOR  MINOR  7F  <failed_request>  <error_code>  [context]  F7
```

| Field | Description |
|---|---|
| failed_request | Request cmd that failed, or `0x00` if unknown |
| error_code | See table below |
| context | Optional; e.g. pin number for PIN_NOT_FOUND |

### Error codes

| Code | Name | Typical cause |
|---|---|---|
| `0x01` | BAD_VERSION | Packet MAJOR ≠ device MAJOR |
| `0x02` | UNKNOWN_COMMAND | Unrecognized CMD byte |
| `0x03` | DECODE_FAILED | Short/malformed packet or bad payload |
| `0x04` | PIN_NOT_FOUND | GET/DELETE for unconfigured pin (context = pin) |
| `0x05` | NOT_READY | Handler not registered on device |
| `0x06` | OPERATION_FAILED | Reserved for handler-level failures |

## Examples

### Set pin 3 as pull-up input → Note On, note 60, channel 1

Request:

```
F0  7D  00  0C  01  03  02  00  01  48  3C  00  7F  F7
         │   │   │   │   │   │   │   │   │   │   └── max
         │   │   │   │   │   │   │   │   └── note 60
         │   │   │   │   │   │   │   └── Note On / 2 = 0x48
         │   │   │   │   │   │   └── channel 1
         │   │   │   │   │   └── threshold 0
         │   │   │   │   └── INPUT_PULLUP
         │   │   │   └── pin 3
         │   │   └── SET_PIN_CONFIG
         │   └── minor (example: 12)
         └── major
```

Success response (`0x41`) repeats the same 8-byte config after the header.

### Get MAC

Request:

```
F0  7D  00  0C  06  F7
```

Response:

```
F0  7D  00  0C  46  [12 nibbles]  F7
```

### Reset

Request:

```
F0  7D  00  0C  09  F7
```

Response:

```
F0  7D  00  0C  49  F7
```

### Error: pin 7 not found (GET_PIN_CONFIG)

```
F0  7D  00  0C  7F  02  04  07  F7
              │   │   │   └── pin 7
              │   │   └── PIN_NOT_FOUND
              │   └── failed GET_PIN_CONFIG (0x02)
              └── ERROR
```

## Host implementation notes

- **Web MIDI:** request SysEx access (`{ sysex: true }`). Incoming SysEx includes `F0`/`F7` in `event.data`.
- **Python (wizard):** [`scripts/wizard/enomik_sysex.py`](../scripts/wizard/enomik_sysex.py) — builders and `parse()` for inner data.
- **Tests:** Catch2 encoder tests in [`test/native/test_enomik_sysex.cpp`](../test/native/test_enomik_sysex.cpp).

When adding a new command:

1. Assign the next request byte (≤ `0x3E` recommended).
2. Success response = request + `0x40`.
3. Update `enomik_sysex.h`, `enomik_sysex.py`, tests, and this document.
4. Do not use `0x7F` except for errors.

## Version history (protocol semantics)

| Change | Notes |
|---|---|
| Version header on all packets | `MAJOR` / `MINOR` after manufacturer ID |
| Unified success responses | All success cmds = request + 64 |
| GET_PEERS response | Includes version header (not legacy `7D 48 …`) |
| GET_VERSION | Wired and responds with `0x4A` |
| Error response | Global `0x7F` with failed_request + error_code |

Hosts built for older firmware may need updates for response command bytes and error handling.
