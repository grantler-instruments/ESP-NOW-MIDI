# enomik Pd abstractions

Vanilla Pure Data abstractions for the [enomik SysEx configuration protocol](../../docs/enomik-sysex-protocol.md). Open `../example_sysex_api.pd` for every command wired up.

Add `[declare -path enomik]` (relative to your patch) to use them. Every request abstraction takes the MIDI out port as argument (default 1).

| Abstraction | Inlet |
|---|---|
| `enomik.set_pin_config` | `pin mode threshold channel type note min max` (type as MIDI status, e.g. 144, or already halved) |
| `enomik.get_pin_config`, `enomik.delete_pin_config` | pin |
| `enomik.get_all_pin_configs`, `enomik.clear_pin_configs` | bang |
| `enomik.add_peer` | `symbol 84:F7:03:F2:54:62` or 6 octets |
| `enomik.get_peer` | index |
| `enomik.get_all_peers`, `enomik.get_mac` | bang |
| `enomik.set_name` | name (max 16 chars), bang clears |
| `enomik.get_name` | bang |
| `enomik.set_midi_loopback`, `enomik.set_power_save` | 0 / 1 |
| `enomik.get_midi_loopback`, `enomik.get_power_save` | bang |
| `enomik.set_raw_stream` | `pin enable interval_ms`, re-sent every 5 s while on |
| `enomik.get_config`, `enomik.get_version`, `enomik.reset` | bang |

Replies: `[enomik.recv]` → `[enomik.parse]` → `[print]`. `enomik.recv` prints errors by name and sends them out its right outlet. `enomik.parse` outputs messages such as `pin_config …`, `peer 0 84:F7:03:F2:54:62`, `name pad left`, `version 0 19 0`, `stream_end pins` and `config_done`.

Building blocks: `enomik.send` (frames `CMD payload…` as SysEx), `enomik.mac` / `enomik.mac2sym` (MAC ↔ 12 nibbles). Send `1` to `enomik.debug` to print every frame sent.

The protocol version (`0 19`) is set in `enomik.send.pd`; update it with `include/version.h` when the major version changes.
