"""Unit tests for the CircuitPython esp_now_midi library."""

import re
import sys
import unittest
from pathlib import Path
from unittest.mock import MagicMock, patch

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT))

# espnow and wifi are CircuitPython built-ins; mock them before importing.
sys.modules["espnow"] = MagicMock()


def _empty_espnow(*args, **kwargs):
    # Like the real ESPNow: len() is the number of packets waiting, so an
    # instance with nothing to read is falsy.
    e = MagicMock()
    e.__len__.return_value = 0
    e.__bool__.return_value = False
    return e


sys.modules["espnow"].ESPNow.side_effect = _empty_espnow
sys.modules["espnow"].Peer.side_effect = lambda *a, **k: MagicMock(**k)
WIFI = MagicMock()
sys.modules["wifi"] = WIFI
import esp_now_midi  # noqa: E402
from esp_now_midi import (  # noqa: E402
    ESPNowMidi,
    is_midi_packet,
    MIDI_AFTERTOUCH,
    MIDI_CONTROL_CHANGE,
    MIDI_NOTE_ON,
    MIDI_PITCH_BEND,
    MIDI_PROGRAM_CHANGE,
    MIDI_START,
    MIDI_TIME_CLOCK,
    SYSEX_HEADER_SIZE,
    SYSEX_MAX_MESSAGE,
    SYSEX_MAX_PAYLOAD,
    VERSION,
    get_version,
)


class TestVersion(unittest.TestCase):
    def test_version_matches_version_h(self):
        version_h = (REPO_ROOT / "include" / "version.h").read_text(encoding="utf-8")
        major = int(re.search(r"ESP_NOW_MIDI_VERSION_MAJOR (\d+)", version_h).group(1))
        minor = int(re.search(r"ESP_NOW_MIDI_VERSION_MINOR (\d+)", version_h).group(1))
        patch = int(re.search(r"ESP_NOW_MIDI_VERSION_PATCH (\d+)", version_h).group(1))
        expected = f"{major}.{minor}.{patch}"

        self.assertEqual(VERSION, expected)
        self.assertEqual(get_version(), expected)


class TestMidiPackets(unittest.TestCase):
    def setUp(self):
        self.midi = ESPNowMidi()

    def test_note_on_channel_encoding(self):
        self.assertEqual(
            self.midi._create_packet(MIDI_NOTE_ON, 1, 60, 127),
            bytes([0x90, 60, 127]),
        )
        self.assertEqual(
            self.midi._create_packet(MIDI_NOTE_ON, 16, 60, 127),
            bytes([0x9F, 60, 127]),
        )

    def test_control_change_round_trip(self):
        packet = self.midi._create_packet(MIDI_CONTROL_CHANGE, 3, 7, 100)
        self.assertEqual(self.midi._parse_packet(packet), (MIDI_CONTROL_CHANGE, 3, 7, 100))

    def test_two_byte_channel_messages(self):
        self.assertEqual(
            self.midi._create_packet(MIDI_PROGRAM_CHANGE, 5, 42, 0),
            bytes([0xC4, 42]),
        )
        self.assertEqual(
            self.midi._create_packet(MIDI_AFTERTOUCH, 2, 64, 0),
            bytes([0xD1, 64]),
        )

    def test_system_message_sizes(self):
        self.assertEqual(
            self.midi._create_packet(MIDI_START, 0, 0, 0),
            bytes([0xFA]),
        )
        self.assertEqual(
            self.midi._create_packet(MIDI_TIME_CLOCK, 0, 0, 0),
            bytes([0xF8]),
        )

        status, channel, _, _ = self.midi._parse_packet(bytes([0xFA]))
        self.assertEqual(status, MIDI_START)
        self.assertEqual(channel, 0)

    def test_pitch_bend_round_trip(self):
        for signed in (-8192, 0, 8191):
            raw = signed + 8192
            packet = self.midi._create_packet(
                MIDI_PITCH_BEND,
                1,
                raw & 0x7F,
                (raw >> 7) & 0x7F,
            )
            _, _, lsb, msb = self.midi._parse_packet(packet)
            decoded = ((msb << 7) | lsb) - 8192
            self.assertEqual(decoded, signed)


PEER = b"\x01\x02\x03\x04\x05\x06"
OTHER = b"\x0A\x0B\x0C\x0D\x0E\x0F"


def started_midi(*peers):
    midi = ESPNowMidi()
    assert midi.begin()
    for mac in peers:
        assert midi.add_peer(mac)
    return midi


class TestLibraryApi(unittest.TestCase):
    def setUp(self):
        WIFI.reset_mock()
        WIFI.radio.connected = False

    def test_begin_moves_radio_to_channel(self):
        midi = ESPNowMidi()
        self.assertTrue(midi.begin(channel=11))

        self.assertIsNotNone(midi.e)
        WIFI.radio.start_ap.assert_called_once()
        self.assertEqual(WIFI.radio.start_ap.call_args.kwargs["channel"], 11)
        WIFI.radio.stop_ap.assert_called_once()

    def test_begin_leaves_connected_radio_alone(self):
        WIFI.radio.connected = True
        WIFI.radio.ap_info.channel = 1

        self.assertTrue(ESPNowMidi().begin())

        WIFI.radio.start_ap.assert_not_called()

    def test_begin_again_removes_peers(self):
        midi = started_midi(PEER)
        old = midi.e
        peer_obj = old.peers.append.call_args[0][0]

        self.assertTrue(midi.begin())

        old.peers.remove.assert_called_once_with(peer_obj)
        old.deinit.assert_called_once()
        self.assertEqual(midi.peers, [])

    def test_add_send_remove_use_peer_objects(self):
        midi = started_midi(PEER)
        peer_obj = midi.e.peers.append.call_args[0][0]
        self.assertEqual(peer_obj.mac, PEER)
        self.assertEqual(peer_obj.channel, 0)  # follow the radio's channel

        self.assertTrue(midi.send_note_on(60, 100, 1))
        midi.e.send.assert_called_once_with(bytes([0x90, 60, 100]), peer_obj)

        self.assertTrue(midi.remove_peer(PEER))
        midi.e.peers.remove.assert_called_once_with(peer_obj)
        self.assertEqual(midi.peers, [])

    def test_send_failure_returns_false(self):
        midi = started_midi(PEER)
        midi.e.send.side_effect = RuntimeError("ESP-NOW error 0x306a")

        self.assertFalse(midi.send_note_on(60, 100, 1))
        self.assertIsNone(midi.get_last_send_time())

    def test_read_dispatches_until_read_returns_none(self):
        midi = started_midi(PEER)
        packet = MagicMock(mac=PEER, msg=bytes([0x91, 64, 1]))
        midi.e.read.side_effect = [packet, None]
        notes = []
        midi.set_handle_note_on(lambda ch, n, v: notes.append((ch, n, v)))

        midi.read()

        self.assertEqual(notes, [(2, 64, 1)])


class TestReceive(unittest.TestCase):
    def setUp(self):
        self.midi = started_midi()

    def test_is_midi_packet(self):
        self.assertTrue(is_midi_packet(bytes([0x90, 60, 100])))
        self.assertTrue(is_midi_packet(bytes([0xC0, 5])))
        self.assertTrue(is_midi_packet(bytes([0xFA])))
        self.assertFalse(is_midi_packet(b""))
        self.assertFalse(is_midi_packet(bytes([0x40, 1, 2])))      # no status byte
        self.assertFalse(is_midi_packet(bytes([0x90, 60])))        # too short
        self.assertFalse(is_midi_packet(bytes([0xC0, 5, 0])))      # too long
        self.assertFalse(is_midi_packet(bytes([0x90, 0x80, 1])))   # data byte >= 0x80
        self.assertFalse(is_midi_packet(bytes([0xF4])))            # undefined
        self.assertFalse(is_midi_packet(bytes([0xF0, 1, 2, 3, 4, 5, 6])))  # not a frame

    def test_foreign_packet_ignored_and_sender_not_added(self):
        notes = []
        self.midi.set_handle_note_on(lambda *a: notes.append(a))

        self.midi._on_data_recv(OTHER, bytes([0x90, 60]))
        self.midi._on_data_recv(OTHER, b"hello world")

        self.assertEqual(notes, [])
        self.assertEqual(self.midi.peers, [])
        self.assertIsNone(self.midi.last_sender_mac())

    def test_valid_packet_adds_sender_and_tracks_last_seen(self):
        self.midi._on_data_recv(OTHER, bytes([0x90, 60, 100]))

        self.assertEqual(self.midi.peers, [OTHER])
        self.assertEqual(self.midi.last_sender_mac(), OTHER)
        self.assertIsNotNone(self.midi.get_peer_last_seen(OTHER))
        self.assertIsNone(self.midi.get_peer_last_seen(PEER))

    def test_handle_message_before_type_handler(self):
        calls = []
        self.midi.set_handle_message(lambda mac, m: calls.append(("msg", mac, m)))
        self.midi.set_handle_control_change(lambda ch, c, v: calls.append(("cc", ch, c, v)))

        self.midi._on_data_recv(PEER, bytes([0xB2, 7, 99]))

        self.assertEqual(calls, [
            ("msg", PEER, (esp_now_midi.MIDI_CONTROL_CHANGE, 3, 7, 99)),
            ("cc", 3, 7, 99),
        ])

    def test_full_peer_table_does_not_try_to_add(self):
        self.midi._max_peers = 1
        self.midi._on_data_recv(PEER, bytes([0xF8]))
        with patch.object(self.midi, "add_peer") as add_peer:
            self.midi._on_data_recv(OTHER, bytes([0xF8]))
        add_peer.assert_not_called()
        self.assertEqual(self.midi.last_sender_mac(), OTHER)

    def test_last_send_time(self):
        self.assertIsNone(self.midi.get_last_send_time())
        self.midi.add_peer(PEER)
        self.midi.send_clock()
        self.assertIsNotNone(self.midi.get_last_send_time())


class TestSysExTransport(unittest.TestCase):
    def setUp(self):
        self.peer = PEER
        self.midi = started_midi(self.peer)
        self.received = []
        self.midi.set_handle_sysex(lambda data, length: self.received.append((data, length)))

    def test_send_sysex_single_fragment(self):
        payload = bytes([0xF0, 0x7D, 0x01, 0xF7])
        self.assertTrue(self.midi.send_sysex(payload))
        self.midi.e.send.assert_called_once()
        frame = self.midi.e.send.call_args[0][0]
        self.assertEqual(frame[0], 0xF0)
        self.assertEqual(len(frame), SYSEX_HEADER_SIZE + len(payload))
        self.assertEqual(bytes(frame[SYSEX_HEADER_SIZE:]), payload)

    def test_send_and_recv_multi_fragment(self):
        payload = bytes([(i * 3) & 0xFF for i in range(SYSEX_MAX_PAYLOAD + 1)])
        self.assertTrue(self.midi.send_sysex(payload))
        self.assertEqual(self.midi.e.send.call_count, 2)

        for call in self.midi.e.send.call_args_list:
            frame = call[0][0]
            self.midi._on_data_recv(self.peer, frame)

        self.assertEqual(len(self.received), 1)
        self.assertEqual(self.received[0][1], len(payload))
        self.assertEqual(self.received[0][0], payload)

    def test_sysex_from_gets_sender_mac(self):
        got = []
        self.midi.set_handle_sysex_from(lambda mac, data, length: got.append((mac, data)))
        msg = bytes([0xF0, 0x7D, 0x01, 0xF7])
        self.midi.send_sysex(msg)

        self.midi._on_data_recv(self.peer, self.midi.e.send.call_args[0][0])

        self.assertEqual(got, [(self.peer, msg)])
        self.assertEqual(len(self.received), 1)

    def test_stale_fragments_are_dropped(self):
        payload = bytes(SYSEX_MAX_PAYLOAD + 1)
        self.midi.send_sysex(payload)
        first, second = [c[0][0] for c in self.midi.e.send.call_args_list]
        now = [1000]
        with patch.object(esp_now_midi, "_now_ms", lambda: now[0]):
            self.midi._on_data_recv(self.peer, first)
            now[0] += esp_now_midi.SYSEX_REASSEMBLY_TIMEOUT_MS + 1
            self.midi._on_data_recv(self.peer, second)

        self.assertEqual(self.received, [])

    def test_announce_wire_format(self):
        self.assertTrue(self.midi.send_announce("client_test"))
        frame = self.midi.e.send.call_args[0][0]
        major, minor, patch_ = (int(x) for x in VERSION.split("."))
        self.assertEqual(
            bytes(frame[SYSEX_HEADER_SIZE:]),
            bytes([0xF0, 0x7D, major, minor, 0x15, 0x00, major, minor, patch_])
            + b"client_test" + bytes([0xF7]),
        )

    def test_reject_oversized(self):
        self.assertFalse(self.midi.send_sysex(bytes(SYSEX_MAX_MESSAGE + 1)))


if __name__ == "__main__":
    unittest.main()
