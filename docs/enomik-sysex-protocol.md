# Enomik SysEx configuration protocol

The enomik configuration protocol lets a host configure an ESP-NOW MIDI client over **MIDI System Exclusive (SysEx)**. It is implemented in firmware by `enomik::Client` / `enomik::IO` and used by the [enomik web app](https://grantler-instruments.github.io/enomik-app/) and the library test wizard.

The dongle (`enomik::Dongle`) answers a subset from its USB host: `GET_MAC`, `ADD_PEER`, `GET_PEER`, `GET_ALL_PEERS`, `RESET` and `GET_VERSION`. Its peers are the live ESP-NOW list, auto-discovered clients included. `RESET` removes all peers (live and stored) and turns power save off; auto-discovered clients come back as soon as they send again. Every other command gets `ERROR_RESPONSE` with `NOT_READY` (`0x05`). Config SysEx sent to the dongle is never forwarded to clients. More commands can be added with `Dongle::sysExHandler()`.

All multi-byte values in SysEx payloads are **7-bit** (0–127), as required by MIDI.

## Source code

| File | Role |
|---|---|
| [`enomik_sysex_codec.h`](https://github.com/grantler-instruments/ESP-NOW-MIDI/blob/main/include/enomik_sysex_codec.h) | Protocol constants, `SysExPacket`, `SysExEncoder`, `SysExDecoder` — pure encode/decode, no Arduino |
| [`enomik_sysex.h`](https://github.com/grantler-instruments/ESP-NOW-MIDI/blob/main/include/enomik_sysex.h) | `SysExHandler` — incoming command routing, callbacks, `EspNowMidiLog` logging |
| [`enomik_io.h`](https://github.com/grantler-instruments/ESP-NOW-MIDI/blob/main/include/enomik_io.h) / [`enomik_client.h`](https://github.com/grantler-instruments/ESP-NOW-MIDI/blob/main/enomik_client.h) | Pin config, NVS, ESP-NOW, USB MIDI wiring |
| [`scripts/wizard/enomik_sysex.py`](https://github.com/grantler-instruments/ESP-NOW-MIDI/blob/main/scripts/wizard/enomik_sysex.py) | Host-side builders and parser (wizard, tests) |

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

**Compatibility:** firmware accepts packets whose **MAJOR** equals `ESP_NOW_MIDI_VERSION_MAJOR` from [`version.h`](https://github.com/grantler-instruments/ESP-NOW-MIDI/blob/main/include/version.h). Mismatch yields an error response (see [Errors](#errors)).

When building messages for Web MIDI / `mido`, the inner data is everything **between** `F0` and `F7` (i.e. starting with `7D`).

## Command numbering

| Kind | Rule | Range (current) |
|---|---|---|
| Request | assigned sequentially | `0x01` … `0x12`, `0x16` |
| Success response | **request + 64** (`0x40`) | `0x41` … `0x52`, `0x56` |
| Notification | never answered | `0x15` (ANNOUNCE, device → device), `0x17` (RAW_VALUE, device → host) |
| Error response | fixed **`0x7F`** | always `0x7F` |

`0x13` and `0x14` are reserved.

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
| `0x07` | ADD_PEER | [12 MAC nibbles](#mac-address-encoding) (not 6 raw octets) | `0x47` + `1` on success; [errors](#errors) on failure |
| `0x08` | GET_ALL_PEERS | (none) | one `0x48` + [peer entry](#peer-entry-payload) per peer, then empty `0x48` |
| `0x09` | RESET | (none) | `0x49` (empty) |
| `0x0A` | GET_VERSION | (none) | `0x4A` + major + minor + patch |
| `0x0B` | GET_PEER | index (1 byte) | `0x4B` + peer entry, or [error](#errors) |
| `0x0C` | GET_CONFIG | (none) | all `0x44` pin configs, then all `0x48` peer entries, then `0x4E` loopback, then `0x50` power-save, then `0x52` name, then empty `0x4C` |
| `0x0D` | SET_MIDI_LOOPBACK | `0`=off or `1`=on | `0x4D` + same byte, or [error](#errors) |
| `0x0E` | GET_MIDI_LOOPBACK | (none) | `0x4E` + `0`/`1` |
| `0x0F` | SET_POWER_SAVE | `0`=off or `1`=on | `0x4F` + same byte, or [error](#errors) |
| `0x10` | GET_POWER_SAVE | (none) | `0x50` + `0`/`1` |
| `0x11` | SET_NAME | [name](#name-payload) (0–16 bytes, empty = clear) | `0x51` + effective name, or [error](#errors) |
| `0x12` | GET_NAME | (none) | `0x52` + effective name |
| `0x16` | SET_RAW_STREAM | pin + enable + interval (3 bytes) | `0x56` + pin + enable + effective interval, then [RAW_VALUE](#raw-stream) frames while on, or [error](#errors) |

### Pin config payload

Used in SET (`0x01`) requests and in pin-config success responses (`0x41`, `0x42`, `0x44`):

| Offset | Field | Description |
|---|---|---|
| 0 | pin | GPIO pin number |
| 1 | mode | [Pin mode](#pin-modes) |
| 2 | threshold | Touch pins: `0` = continuous, `1`–`127` = on/off at this % change ([touch inputs](#touch-inputs)). Ignored by other modes. |
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

### Peer entry payload

Used in **GET_PEER** (`0x4B`) and **GET_ALL_PEERS** (`0x48`) success responses:

| Offset | Field | Description |
|---|---|---|
| 0 | index | Peer slot in device storage (0 … count−1) |
| 1–12 | MAC | [12 nibbles](#mac-address-encoding) |

Peer **index** is a storage slot, not a permanent ID — it can change when peers are deleted. The MAC is the stable identity.

### Stream end markers

**GET_ALL_PIN_CONFIGS** and **GET_ALL_PEERS** send one entry per message, then a **terminal empty response** with the same response cmd and no payload:

```
F0  7D  MAJOR  MINOR  44  F7     ← pin stream end
F0  7D  MAJOR  MINOR  48  F7     ← peer stream end
```

If there are zero entries, the host receives only the stream-end message.

**GET_CONFIG** uses the same `0x44` and `0x48` entry shapes, then `0x4E` loopback, then `0x50` power-save, then **one** final empty `0x4C` instead of separate pin/peer stream-end markers.

### MAC address encoding

A MAC address is **6 octets** (e.g. `84:F7:03:F2:54:62`). On the SysEx wire it is **always encoded as 12 nibbles** — never as 6 raw bytes.

Each octet is split into two 4-bit values (high nibble, low nibble). Every nibble is sent as its own SysEx data byte in the range **0–15**:

```
Octet   0x84      0xF7      0x03      0xF2      0x54      0x62
Nibble  8    4    F    7    0    3    F    2    5    4    6    2
        └─12 SysEx payload bytes (each 0–15)─────────────────────┘
```

**Correct** — 12 nibbles (used by firmware, wizard, and enomik app deploy):

```
MAC 84:F7:03:F2:54:62  →  08 04 0F 07 00 03 0F 02 05 04 06 02
```

**Wrong** — 6 raw octets (firmware rejects with `DECODE_FAILED`; `decodeMAC` requires length ≥ 12):

```
MAC 84:F7:03:F2:54:62  →  84 F7 03 F2 54 62   ← do not use
```

Hosts may build nibbles by splitting the hex string **one character at a time** (`"84F7…".split("")` → parse each digit as 0–15). That yields 12 nibbles, not 6 bytes — even though the address itself is 6 octets. Do **not** parse two-character pairs into full octets and send those six values.

Encode: `hi = (octet >> 4) & 0x0F`, `lo = octet & 0x0F`. Decode: `octet = (hi << 4) | lo`.

Used in **ADD_PEER** requests, **GET_MAC** responses (`0x46`), and [peer entry](#peer-entry-payload) payloads.

## Success response details

### SET_PIN_CONFIG → `0x41`

Echoes the stored configuration (same 8-byte payload as request).

### GET_PIN_CONFIG → `0x42`

Returns configuration for the requested pin. If the pin is not configured, see [PIN_NOT_FOUND](#error-codes).

### GET_ALL_PIN_CONFIGS → `0x44`

Sends **one SysEx message per pin** (each with cmd `0x44` and an 8-byte [pin config](#pin-config-payload)), then an empty `0x44` [stream end](#stream-end-markers).

### DELETE_PIN_CONFIG → `0x45`

Payload: deleted pin number (1 byte).

### GET_MAC → `0x46`

Payload: 12 MAC nibbles.

### ADD_PEER → `0x47`

On success only:

```
F0  7D  MAJOR  MINOR  47  01  F7
```

Any failure returns **`0x7F`** (never `0x47` + `0`). See [error codes](#error-codes).

| Error | Code | When |
|---|---|---|
| `DECODE_FAILED` | `0x03` | Payload is not 12 MAC nibbles |
| `NOT_READY` | `0x05` | ADD_PEER handler not registered |
| `OPERATION_FAILED` | `0x06` | ESP-NOW or internal failure |
| `PEER_TABLE_FULL` | `0x08` | No free peer slots |
| `PEER_ALREADY_EXISTS` | `0x09` | MAC already stored |

Examples (minor `0x0D` = v0.13):

```
Success:              F0 7D 00 0D 47 01 F7
Table full:           F0 7D 00 0D 7F 07 08 F7
Already exists:       F0 7D 00 0D 7F 07 09 F7
ESP-NOW failed:       F0 7D 00 0D 7F 07 06 F7
Bad MAC (6 raw bytes): F0 7D 00 0D 7F 07 03 F7
```

### GET_ALL_PEERS → `0x48`

Sends **one SysEx message per peer** (each with cmd `0x48` and a [peer entry](#peer-entry-payload)), then an empty `0x48` [stream end](#stream-end-markers).

### GET_PEER → `0x4B`

Returns the peer at the requested storage **index**. If the index is out of range, see [PEER_NOT_FOUND](#error-codes).

### GET_CONFIG → `0x4C`

One-shot full board snapshot for **pins + peers + device flags** (v0.14+; v0.13 was pins + peers only):

1. Every stored pin config as `0x44` (same 8-byte payload as GET_ALL_PIN_CONFIGS).
2. Every stored peer as `0x48` (same [peer entry](#peer-entry-payload) as GET_ALL_PEERS).
3. One `0x4E` + loopback byte (same payload as [GET_MIDI_LOOPBACK](#get_midi_loopback-0x4e)).
4. One `0x50` + power-save byte (same payload as [GET_POWER_SAVE](#get_power_save-0x50)).
5. One `0x52` + client name (same payload as [GET_NAME](#get_name-0x52), v0.19+).
6. Empty `0x4C` — **done** (no separate empty `0x44` / `0x48` in this stream).

Order is always **pins, peers, loopback, power-save, name**. Empty board on v0.14+ → flag responses (usually `0`) then empty `0x4C`. Older firmware may omit newer flag responses and end on `0x4C` only. Hosts should ignore unknown mid-stream response cmds until `0x4C`.

### SET_MIDI_LOOPBACK → `0x4D`

Payload: one byte, `0` (off) or `1` (on). Other values → [DECODE_FAILED](#error-codes). Success echoes the same byte in `0x4D`. When enabled, `enomik::Client` locally re-dispatches each outgoing channel/realtime MIDI message to receive handlers (soft thru); SysEx is not looped. Nested sends from those handlers (e.g. echo sketches) are not looped again, so `send → handler → send` cannot recurse.

### GET_MIDI_LOOPBACK → `0x4E`

Payload: one byte, `0` or `1`.

### SET_POWER_SAVE → `0x4F`

Payload: one byte, `0` (off) or `1` (on). Other values → [DECODE_FAILED](#error-codes). Success echoes the same byte in `0x4F`. When enabled, applies `esp_now_midi::setReducePowerAtCostOfLatency(true)` (modem sleep + lower TX power). Applied immediately and persisted for the next boot. Default is off (lower latency).

### GET_POWER_SAVE → `0x50`

Payload: one byte, `0` or `1`.

### SET_NAME → `0x51`

<a id="name-payload"></a>Payload: the name as raw bytes, printable ASCII (`0x20`–`0x7E`), at most 16, no terminator. Longer names or other bytes → [DECODE_FAILED](#error-codes). An empty payload clears the app override. Success returns the **effective** name in `0x51` (after clearing: the sketch default).

The effective name is the app override if one is stored, else the sketch's `Client::setName()` default, else `"enomik"`. The override is stored in NVS and survives reboots. It is dropped by RESET, an empty SET_NAME, or flashing a different firmware (the client compares the firmware's ELF SHA-256 on boot), so a new sketch always starts with its own name.

```
Set "pad left":  F0 7D 00 13 11 70 61 64 20 6C 65 66 74 F7
Response:        F0 7D 00 13 51 70 61 64 20 6C 65 66 74 F7
Clear override:  F0 7D 00 13 11 F7
```

### GET_NAME → `0x52`

Payload: effective name (0–16 printable ASCII bytes).

### RESET → `0x49`

Empty payload. Clears pin configs, stored flags and the name override, and invokes device reset hooks.

### GET_VERSION → `0x4A`

Payload: major (1 byte), minor (1 byte), patch (1 byte, v0.19+) — protocol/library version. Older firmware sends only major and minor; hosts should read the first two bytes and treat patch as optional.

## Touch inputs

Raw touch readings differ by chip: about 10–80 on the ESP32, lower on touch; about 10,000–200,000+ on the ESP32-S2/S3/P4, higher on touch. The C3/C6/H2 have no touch sensor. So the firmware works with the **change from the untouched reading** (baseline) in the touch direction, and the same pin config behaves the same on every chip:

- **Baseline:** average of 16 readings when the pin is configured or the device boots, so keep fingers off the pad then. It follows slow drift (temperature, humidity) while the pad is not touched. A SET_PIN_CONFIG that changes only the threshold keeps the baseline; any other change measures it again.
- **Threshold `0` (continuous):** 0 at the baseline, 127 at the strongest touch seen since the baseline was measured (at least 5 % change, `TOUCH_MIN_PEAK_PCT`), mapped onto min/max (min > max inverts). The first touch after boot reads 127 until a firmer one raises the peak.
- **Threshold `1`–`127` (on/off):** sends max when the change reaches threshold %, min when it falls below ¾ of it.

To tune a pad, use the [raw stream](#raw-stream): touch it, read `percent`, and set the threshold to about half of what a light touch reaches.

## Raw stream

Live readings of input pins, e.g. for a tuning view in an app: turn it on with SET_RAW_STREAM, adjust the pin with SET_PIN_CONFIG while frames keep coming, turn it off.

**SET_RAW_STREAM (`0x16`)** payload:

| Byte | Value |
|---|---|
| 0 | pin, or `0x7F` for every configured input pin |
| 1 | `1` on, `0` off |
| 2 | frame interval in 5 ms units; `0` = 50 ms; shorter than 20 ms is raised to 20 ms |

The response `0x56` repeats pin and enable with the interval in effect. Turning it on for a pin that is not a configured input answers [PIN_NOT_FOUND](#error-codes). The stream is not stored: it ends with SET_RAW_STREAM off, RESET, a reboot, or **10 s without any SysEx from the host**. Send SET_RAW_STREAM on again every few seconds as a keepalive.

**RAW_VALUE (`0x17`)**, one per streamed pin each interval, never answered:

| Byte | Field | Value |
|---|---|---|
| 0 | pin | |
| 1 | mode | [Pin mode](#pin-modes) |
| 2–6 | raw | last reading: `touchRead()`, `analogRead()` or digital 0/1 |
| 7–11 | baseline | touch: untouched reading; else 0 |
| 12–16 | peak | touch continuous: change from baseline that maps to 127; else 0 |
| 17 | percent | touch: change from baseline in %, at most 127; else 0 |
| 18 | norm | reading as 0–127 (touch threshold mode: 0 / 127) |
| 19 | threshold | the pin's threshold |
| 20 | active | touch threshold mode: `1` while on |

32-bit values are 5 bytes of 7 bits, most significant first: `value = b0<<28 | b1<<21 | b2<<14 | b3<<7 | b4`.

```
Stream pin 4 every 50 ms:  F0 7D 00 13 16 04 01 0A F7
Response:                  F0 7D 00 13 56 04 01 0A F7
Stop:                      F0 7D 00 13 16 04 00 00 F7
```

Frames go to USB like every config reply, so the client must be connected over USB.

## ANNOUNCE (`0x15`)

A device tells its ESP-NOW peers who it is. It is a notification: receivers never answer, and the dongle never forwards it to the USB host. A Client sends it after `begin()`, after a peer is added, and as heartbeat when it sent nothing else over ESP-NOW for `setHeartbeatInterval()` (default 60 s).

| Byte | Value |
|---|---|
| 0 | kind: `0x00` client, `0x01` dongle |
| 1–3 | library version: major, minor, patch |
| 4… | name, 0–16 printable ASCII bytes |

```
F0 7D 00 13 15 00 00 13 00 63 6C 69 65 6E 74 5F 74 65 73 74 F7   ← client "client_test", v0.19.0
```

The dongle keeps the last announce per peer (`Dongle::getPeerAnnounce()`, `getPeerName()`). Any valid packet from a peer counts as a sign of life: `Dongle::getPeerStatus()` is Online when the peer was heard within the peer timeout (default 180 s), Offline when it announced before but has been silent longer, and Unknown otherwise (including older clients that never announce).

## Errors

Errors use a **single global command** `0x7F`, not request + 128 (invalid in 7-bit MIDI).

```
F0  7D  MAJOR  MINOR  7F  <failed_request>  <error_code>  [context]  F7
```

| Field | Description |
|---|---|
| failed_request | Request cmd that failed, or `0x00` if unknown |
| error_code | See table below |
| context | Optional; e.g. pin number for PIN_NOT_FOUND, peer index for PEER_NOT_FOUND |

### Error codes

| Code | Name | Typical cause |
|---|---|---|
| `0x01` | BAD_VERSION | Packet MAJOR ≠ device MAJOR |
| `0x02` | UNKNOWN_COMMAND | Unrecognized CMD byte |
| `0x03` | DECODE_FAILED | Short/malformed packet or bad payload |
| `0x04` | PIN_NOT_FOUND | GET/DELETE for unconfigured pin (context = pin) |
| `0x05` | NOT_READY | Handler not registered on device |
| `0x06` | OPERATION_FAILED | ESP-NOW / internal handler failure |
| `0x07` | PEER_NOT_FOUND | GET_PEER for invalid index (context = index) |
| `0x08` | PEER_TABLE_FULL | ADD_PEER when peer table is full |
| `0x09` | PEER_ALREADY_EXISTS | ADD_PEER for a MAC already stored |

## Examples

### Set pin 3 as pull-up input → Note On, note 60, channel 1

Request:

```
F0  7D  00  0D  01  03  02  00  01  48  3C  00  7F  F7
         │   │   │   │   │   │   │   │   │   │   └── max
         │   │   │   │   │   │   │   │   └── note 60
         │   │   │   │   │   │   │   └── Note On / 2 = 0x48
         │   │   │   │   │   │   └── channel 1
         │   │   │   │   │   └── threshold 0
         │   │   │   │   └── INPUT_PULLUP
         │   │   │   └── pin 3
         │   │   └── SET_PIN_CONFIG
         │   └── minor (example: 13)
         └── major
```

Success response (`0x41`) repeats the same 8-byte config after the header.

### Get MAC

Request:

```
F0  7D  00  0D  06  F7
```

Response:

```
F0  7D  00  0D  46  [12 nibbles]  F7
```

### Add peer (dongle MAC)

Request for `84:F7:03:F2:54:62` — note **12** nibble bytes after the header, not 6 octets:

```
F0  7D  00  0D  07  08 04 0F 07 00 03 0F 02 05 04 06 02  F7
              │   └────────────────── 12 nibbles ──────────┘
              └── ADD_PEER
```

Response on success:

```
F0  7D  00  0D  47  01  F7
```

### Reset

Request:

```
F0  7D  00  0D  09  F7
```

Response:

```
F0  7D  00  0D  49  F7
```

### Error: pin 7 not found (GET_PIN_CONFIG)

```
F0  7D  00  0D  7F  02  04  07  F7
              │   │   │   └── pin 7
              │   │   └── PIN_NOT_FOUND
              │   └── failed GET_PIN_CONFIG (0x02)
              └── ERROR
```

## Host implementation notes

- **Web MIDI:** request SysEx access (`{ sysex: true }`). Incoming SysEx includes `F0`/`F7` in `event.data`.
- **Firmware codec:** [`enomik_sysex_codec.h`](https://github.com/grantler-instruments/ESP-NOW-MIDI/blob/main/include/enomik_sysex_codec.h) — enums and encode/decode (testable natively without Arduino).
- **Firmware handler:** [`enomik_sysex.h`](https://github.com/grantler-instruments/ESP-NOW-MIDI/blob/main/include/enomik_sysex.h) — routes requests to IO callbacks and sends responses.
- **Python (wizard):** [`scripts/wizard/enomik_sysex.py`](https://github.com/grantler-instruments/ESP-NOW-MIDI/blob/main/scripts/wizard/enomik_sysex.py) — builders and `parse()` for inner data.
- **Constant sync (CI):** [`scripts/check_sysex_constants.py`](https://github.com/grantler-instruments/ESP-NOW-MIDI/blob/main/scripts/check_sysex_constants.py) — verifies C++ codec and Python enums stay aligned.
- **Tests:** Catch2 codec tests in [`test/native/test_enomik_sysex.cpp`](https://github.com/grantler-instruments/ESP-NOW-MIDI/blob/main/test/native/test_enomik_sysex.cpp), handler tests in [`test/native/test_enomik_sysex_handler.cpp`](https://github.com/grantler-instruments/ESP-NOW-MIDI/blob/main/test/native/test_enomik_sysex_handler.cpp), CI constant check in [`test/protocol/test_sysex_constants.py`](https://github.com/grantler-instruments/ESP-NOW-MIDI/blob/main/test/protocol/test_sysex_constants.py).

When adding a new command:

1. Assign the next request byte (≤ `0x3E` recommended).
2. Success response = request + `0x40`.
3. Update `enomik_sysex_codec.h`, `enomik_sysex.py`, tests, and this document.
4. Do not use `0x7F` except for errors.

## Version history (protocol semantics)

| Change | Notes |
|---|---|
| v0.19 raw stream + touch | `SET_RAW_STREAM` (`0x16`/`0x56`) and `RAW_VALUE` (`0x17`) notifications; touch threshold is % change from a baseline instead of a raw reading |
| v0.19 announce | `ANNOUNCE` (`0x15`) replaces the CC 127 = 127 on channel 16 handshake; sent at boot, after pairing and as idle heartbeat |
| v0.19 client name | `SET_NAME`/`GET_NAME` (`0x11`/`0x12`); `GET_CONFIG` streams `0x52` before empty `0x4C`; GET_VERSION adds patch byte |
| v0.14 MIDI loopback + power save | `SET_MIDI_LOOPBACK`/`GET_MIDI_LOOPBACK` (`0x0D`/`0x0E`); `SET_POWER_SAVE`/`GET_POWER_SAVE` (`0x0F`/`0x10`); `GET_CONFIG` streams `0x4E` then `0x50` before empty `0x4C` |
| v0.13 ADD_PEER errors | Failures use `0x7F` with PEER_TABLE_FULL / PEER_ALREADY_EXISTS / OPERATION_FAILED |
| v0.13 GET_CONFIG | One request streams `0x44` pins + `0x48` peers + empty `0x4C` done |
| v0.13 peer model | GET_PEER (`0x0B`), GET_ALL_PEERS streams one `0x48` per peer + end marker; PEER_NOT_FOUND |
| v0.13 stream end | GET_ALL_PIN_CONFIGS and GET_ALL_PEERS terminate with empty response packet |
| Version header on all packets | `MAJOR` / `MINOR` after manufacturer ID |
| Unified success responses | All success cmds = request + 64 |
| GET_ALL_PEERS response | Includes version header (not legacy `7D 48 …`) |
| GET_VERSION | Wired and responds with `0x4A` |
| Error response | Global `0x7F` with failed_request + error_code |

Hosts built for older firmware may need updates for response command bytes and error handling.
