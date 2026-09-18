"""Unit tests for the CircuitPython esp_now_midi library."""

import re
import sys
import unittest
from pathlib import Path
from unittest.mock import MagicMock

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT))

# espnow is a CircuitPython built-in; mock it before importing the library.
sys.modules["espnow"] = MagicMock()
from esp_now_midi import (  # noqa: E402
    ESPNowMidi,
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


class TestLibraryApi(unittest.TestCase):
    def test_begin_initializes_espnow(self):
        midi = ESPNowMidi()
        self.assertTrue(midi.begin())

        self.assertIsNotNone(midi.e)
        midi.e.active.assert_called_once_with(True)

    def test_clear_peers_removes_all_peers(self):
        midi = ESPNowMidi()
        midi.e = MagicMock()
        peer = b"\x01\x02\x03\x04\x05\x06"
        midi.peers = [peer]

        midi.clear_peers()

        midi.e.remove_peer.assert_called_once_with(peer)
        self.assertEqual(midi.peers, [])


class TestSysExTransport(unittest.TestCase):
    def setUp(self):
        self.midi = ESPNowMidi()
        self.midi.e = MagicMock()
        self.peer = b"\x01\x02\x03\x04\x05\x06"
        self.midi.peers = [self.peer]
        self.received = []
        self.midi.set_handle_sysex(lambda data, length: self.received.append((data, length)))

    def test_send_sysex_single_fragment(self):
        payload = bytes([0xF0, 0x7D, 0x01, 0xF7])
        self.assertTrue(self.midi.send_sysex(payload))
        self.midi.e.send.assert_called_once()
        frame = self.midi.e.send.call_args[0][1]
        self.assertEqual(frame[0], 0xF0)
        self.assertEqual(len(frame), SYSEX_HEADER_SIZE + len(payload))
        self.assertEqual(bytes(frame[SYSEX_HEADER_SIZE:]), payload)

    def test_send_and_recv_multi_fragment(self):
        payload = bytes([(i * 3) & 0xFF for i in range(SYSEX_MAX_PAYLOAD + 1)])
        self.assertTrue(self.midi.send_sysex(payload))
        self.assertEqual(self.midi.e.send.call_count, 2)

        for call in self.midi.e.send.call_args_list:
            frame = call[0][1]
            self.midi._on_data_recv(self.peer, frame)

        self.assertEqual(len(self.received), 1)
        self.assertEqual(self.received[0][1], len(payload))
        self.assertEqual(self.received[0][0], payload)

    def test_reject_oversized(self):
        self.assertFalse(self.midi.send_sysex(bytes(SYSEX_MAX_MESSAGE + 1)))


if __name__ == "__main__":
    unittest.main()
