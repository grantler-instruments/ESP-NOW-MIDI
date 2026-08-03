"""Host-side tests for scripts/wizard/enomik_sysex.py builders and parser."""

from __future__ import annotations

import importlib.util
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]


def load_wizard_sysex():
    path = REPO_ROOT / "scripts" / "wizard" / "enomik_sysex.py"
    spec = importlib.util.spec_from_file_location("enomik_sysex_wizard", path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


sx = load_wizard_sysex()

SAMPLE_MAC = [0x84, 0xF7, 0x03, 0xF2, 0x54, 0x62]


def response(cmd: int, payload: list[int] | None = None) -> list[int]:
    """Build inner SysEx data (no F0/F7), matching firmware responses."""
    return [sx.MANUFACTURER_ID, sx.PROTOCOL_MAJOR, sx.PROTOCOL_MINOR, cmd] + (
        payload or []
    )


class TestWizardBuilders(unittest.TestCase):
    def test_header_uses_protocol_version(self):
        data = sx.build_get_version()
        self.assertEqual(
            data,
            [sx.MANUFACTURER_ID, sx.PROTOCOL_MAJOR, sx.PROTOCOL_MINOR, sx.CMD_GET_VERSION],
        )

    def test_build_get_mac(self):
        self.assertEqual(sx.build_get_mac()[3], sx.CMD_GET_MAC)

    def test_build_get_all_peers(self):
        self.assertEqual(sx.build_get_all_peers()[3], sx.CMD_GET_ALL_PEERS)

    def test_build_get_peer_includes_index(self):
        self.assertEqual(sx.build_get_peer(3), sx._header(sx.CMD_GET_PEER) + [3])

    def test_build_get_config(self):
        self.assertEqual(sx.build_get_config()[3], sx.CMD_GET_CONFIG)

    def test_build_add_peer_encodes_mac_nibbles(self):
        data = sx.build_add_peer(SAMPLE_MAC)
        self.assertEqual(data[3], sx.CMD_ADD_PEER)
        self.assertEqual(data[4:], sx._mac_to_nibbles(SAMPLE_MAC))
        self.assertEqual(len(data[4:]), 12)


class TestWizardParserPeers(unittest.TestCase):
    def test_parse_peer_entry_from_get_all_peers(self):
        payload = [2] + sx._mac_to_nibbles(SAMPLE_MAC)
        parsed = sx.parse(response(sx.RESP_GET_ALL_PEERS, payload))
        self.assertEqual(
            parsed,
            {"cmd": "peer_entry", "index": 2, "mac": SAMPLE_MAC},
        )

    def test_parse_peer_entry_from_get_peer(self):
        payload = [0] + sx._mac_to_nibbles(SAMPLE_MAC)
        parsed = sx.parse(response(sx.RESP_GET_PEER, payload))
        self.assertEqual(
            parsed,
            {"cmd": "peer_entry", "index": 0, "mac": SAMPLE_MAC},
        )

    def test_parse_peer_stream_end(self):
        parsed = sx.parse(response(sx.RESP_GET_ALL_PEERS))
        self.assertEqual(parsed, {"cmd": "peer_stream_end"})

    def test_parse_get_config_ok(self):
        parsed = sx.parse(response(sx.RESP_GET_CONFIG))
        self.assertEqual(parsed, {"cmd": "get_config_ok"})


class TestWizardParserAddPeerAndErrors(unittest.TestCase):
    def test_parse_add_peer_ok(self):
        parsed = sx.parse(response(sx.RESP_ADD_PEER, [1]))
        self.assertEqual(parsed, {"cmd": "add_peer_ok"})

    def test_parse_add_peer_zero_is_not_ok(self):
        # Failures must use ERROR_RESPONSE; success byte 0 is not success.
        parsed = sx.parse(response(sx.RESP_ADD_PEER, [0]))
        self.assertEqual(parsed["cmd"], "unknown")

    def test_parse_error_without_context(self):
        parsed = sx.parse(
            response(
                sx.RESP_ERROR,
                [sx.CMD_ADD_PEER, sx.ERR_PEER_ALREADY_EXISTS],
            )
        )
        self.assertEqual(
            parsed,
            {
                "cmd": "error",
                "failed_request": sx.CMD_ADD_PEER,
                "error_code": sx.ERR_PEER_ALREADY_EXISTS,
                "error": "peer_already_exists",
            },
        )

    def test_parse_error_with_context(self):
        parsed = sx.parse(
            response(
                sx.RESP_ERROR,
                [sx.CMD_GET_PEER, sx.ERR_PEER_NOT_FOUND, 4],
            )
        )
        self.assertEqual(parsed["cmd"], "error")
        self.assertEqual(parsed["failed_request"], sx.CMD_GET_PEER)
        self.assertEqual(parsed["error_code"], sx.ERR_PEER_NOT_FOUND)
        self.assertEqual(parsed["error"], "peer_not_found")
        self.assertEqual(parsed["context"], 4)

    def test_parse_peer_table_full_error(self):
        parsed = sx.parse(
            response(
                sx.RESP_ERROR,
                [sx.CMD_ADD_PEER, sx.ERR_PEER_TABLE_FULL],
            )
        )
        self.assertEqual(parsed["error"], "peer_table_full")


class TestWizardParserMacAndVersion(unittest.TestCase):
    def test_parse_get_mac(self):
        parsed = sx.parse(response(sx.RESP_GET_MAC, sx._mac_to_nibbles(SAMPLE_MAC)))
        self.assertEqual(parsed, {"cmd": "get_mac", "mac": SAMPLE_MAC})

    def test_build_add_peer_mac_round_trip_via_string(self):
        mac = sx.mac_from_string("84:F7:03:F2:54:62")
        self.assertEqual(sx.mac_to_string(mac), "84:F7:03:F2:54:62")
        parsed_req_mac = sx._nibbles_to_mac(sx.build_add_peer(mac)[4:])
        self.assertEqual(parsed_req_mac, mac)

    def test_parse_version(self):
        parsed = sx.parse(
            response(sx.RESP_GET_VERSION, [sx.PROTOCOL_MAJOR, sx.PROTOCOL_MINOR])
        )
        self.assertEqual(
            parsed,
            {
                "cmd": "version",
                "major": sx.PROTOCOL_MAJOR,
                "minor": sx.PROTOCOL_MINOR,
            },
        )

    def test_rejects_non_enomik_manufacturer(self):
        self.assertIsNone(sx.parse([0x7E, 0, 13, sx.RESP_GET_VERSION, 0, 13]))

    def test_rejects_short_packet(self):
        self.assertIsNone(sx.parse([sx.MANUFACTURER_ID, 0, 13]))


if __name__ == "__main__":
    unittest.main()
