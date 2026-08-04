# ESP-NOW MIDI documentation

| Document | Description |
|---|---|
| [enomik-sysex-protocol.md](./enomik-sysex-protocol.md) | Enomik configuration protocol over MIDI SysEx |

**Firmware:** [`enomik_sysex_codec.h`](../enomik_sysex_codec.h) (protocol encode/decode), [`enomik_sysex.h`](../enomik_sysex.h) (handler + `EspNowMidiLog` logging). **Host tools:** [`scripts/wizard/enomik_sysex.py`](../scripts/wizard/enomik_sysex.py).

CI sync check: [`scripts/check_sysex_constants.py`](../scripts/check_sysex_constants.py) (reads enums from `enomik_sysex_codec.h`).

Tests: [`test/native/test_enomik_sysex.cpp`](../test/native/test_enomik_sysex.cpp) (codec), [`test/native/test_enomik_sysex_handler.cpp`](../test/native/test_enomik_sysex_handler.cpp) (handler), [`test/protocol/test_sysex_constants.py`](../test/protocol/test_sysex_constants.py) (CI).
