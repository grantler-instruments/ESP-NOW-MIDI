"""Host-side tests for scripts/wizard/tester.py (board tester logic, no MIDI needed).

Mirrors tester.test.ts and testerState.test.ts in the enomik app.
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts" / "wizard"))

import enomik_sysex as sx  # noqa: E402
import tester as t  # noqa: E402

HEADER = [sx.MANUFACTURER_ID, sx.PROTOCOL_MAJOR, sx.PROTOCOL_MINOR]
MAC_NIBBLES = [8, 4, 15, 7, 0, 3, 15, 2, 5, 4, 0, 6]
MAC = [0x84, 0xF7, 0x03, 0xF2, 0x54, 0x06]


def reply(cmd: int, payload: list[int]) -> list[int]:
    return [*HEADER, cmd, *payload]


def payload(data: list[int]) -> list[int]:
    """Payload of a request built by tester: drop header and command."""
    return data[4:]


class FakeBoard:
    """Answers every request with all of `answers` (inner data), in order."""

    def __init__(self, answers: list[list[int]]) -> None:
        self.answers = answers
        self.sent: list[list[int]] = []

    def __call__(self, data, accept, timeout):
        self.sent.append(data)
        for answer in self.answers:
            parsed = t.parse_reply(answer)
            if parsed is None:
                continue
            if parsed["failed_request"] == data[3] if parsed["cmd"] == "error" else accept(parsed):
                return parsed
        return None


class TestBuilders(unittest.TestCase):
    def test_button_as_input_pullup_sending_cc_pin(self):
        self.assertEqual(t.button_config(16), [*HEADER, 0x01, 16, 0x02, 0, 1, 0x58, 16, 0, 127])

    def test_led_as_output_on_note_pin(self):
        self.assertEqual(payload(t.led_config(15)), [15, 0x01, 0, 1, 0x48, 15, 0, 127])

    def test_pin_note_to_another_number_for_loopback(self):
        self.assertEqual(payload(t.button_config(16, sx.MIDI_NOTE_ON, 15)), [16, 0x02, 0, 1, 0x48, 15, 0, 127])

    def test_touch_in_threshold_mode_sending_cc_pin(self):
        self.assertEqual(payload(t.touch_config(4)), [4, 0x05, 20, 1, 0x58, 4, 0, 127])

    def test_pitch_bend_with_halved_status(self):
        self.assertEqual(payload(t.button_config(16, sx.MIDI_PITCH_BEND))[4], 0x70)


class TestParse(unittest.TestCase):
    def test_pin_config_acks_and_errors(self):
        self.assertEqual(t.parse_reply(reply(0x41, [17, 2, 0, 1, 0x58, 17, 0, 127])), {"cmd": "pin_config", "pin": 17})
        self.assertEqual(
            t.parse_reply(reply(0x7F, [0x07, 0x09])), {"cmd": "error", "failed_request": 0x07, "error_code": 0x09}
        )

    def test_midi_loopback_reply(self):
        self.assertEqual(t.parse_reply(reply(0x4D, [1])), {"cmd": "midi_loopback", "enabled": True})

    def test_mac_replies_including_pre_013_echo(self):
        self.assertEqual(
            t.parse_reply(reply(0x46, MAC_NIBBLES)),
            {"cmd": "mac", "mac": MAC, "header": (sx.PROTOCOL_MAJOR, sx.PROTOCOL_MINOR)},
        )
        self.assertEqual(t.parse_reply([sx.MANUFACTURER_ID, 0, 12, 0x06, *MAC_NIBBLES])["header"], (0, 12))

    def test_truncated_or_invalid_mac_replies(self):
        self.assertIsNone(t.parse_reply(reply(0x46, MAC_NIBBLES[:11])))
        self.assertIsNone(t.parse_reply(reply(0x46, [*MAC_NIBBLES[:11], 16])))

    def test_version_with_and_without_patch(self):
        self.assertEqual(t.parse_reply(reply(0x4A, [0, 18])), {"cmd": "version", "version": (0, 18)})
        self.assertEqual(t.parse_reply(reply(0x4A, [0, 19, 2]))["version"], (0, 19, 2))
        self.assertIsNone(t.parse_reply(reply(0x4A, [0])))

    def test_other_manufacturer(self):
        self.assertIsNone(t.parse_reply([0x7E, 0, 19, 0x46, *MAC_NIBBLES]))


class TestEvents(unittest.TestCase):
    def test_loopback_note_velocity_0_is_release(self):
        self.assertIs(t.note_down([0x90, 15, 127], 15), True)
        self.assertIs(t.note_down([0x90, 15, 0], 15), False)
        self.assertIs(t.note_down([0x80, 15, 64], 15), False)
        self.assertIsNone(t.note_down([0x90, 16, 127], 15))
        self.assertIsNone(t.note_down([0x91, 15, 127], 15))

    def test_presses_and_releases_of_every_type_on_channel_1_only(self):
        self.assertEqual(t.button_event([0xB0, 16, 127]), (16, 0xB0, True))
        self.assertEqual(t.button_event([0x90, 16, 0]), (16, 0x90, False))
        self.assertIs(t.button_event([0x80, 17, 127])[2], True)
        self.assertEqual(t.button_event([0xE0, 0x7F, 0x7F]), (16, 0xE0, True))
        self.assertIs(t.button_event([0xE0, 0, 0])[2], False)
        self.assertIsNone(t.button_event([0xB1, 16, 127]))
        self.assertIsNone(t.button_event([0xB0, 18, 127]))

    def test_touch_pins_only_when_listed(self):
        self.assertIsNone(t.button_event([0xB0, 4, 127]))
        self.assertIs(t.button_event([0xB0, 4, 127], [4])[2], True)


class TestSettings(unittest.TestCase):
    def test_parse_pins(self):
        self.assertEqual(t.parse_pins(" 1, 2 3,"), [1, 2, 3])
        self.assertEqual(t.parse_pins(""), [])
        with self.assertRaises(ValueError):
            t.parse_pins("1, x")

    def test_settings_errors(self):
        self.assertEqual(t.settings_errors(15, [1, 2]), [])
        self.assertEqual(t.settings_errors(16, [1]), ["LED pin: used by a button"])
        self.assertEqual(t.settings_errors(15, [49]), ["touch pins: 0 to 48, comma separated"])
        self.assertEqual(t.settings_errors(15, [1, 1]), ["touch pins: used twice or by a button/LED"])
        self.assertEqual(t.settings_errors(15, [15]), ["touch pins: used twice or by a button/LED"])

    def test_formats(self):
        self.assertEqual(t.version_to_string((0, 19, 0)), "0.19.0")
        self.assertEqual(t.version_to_string((0, 12)), "0.12")
        self.assertEqual(
            t.BoardInfo([0, 1, 2, 3, 4, 255], (0, 19, 1)).text(), "firmware 0.19.1 · MAC 00:01:02:03:04:FF"
        )
        self.assertEqual(t.BoardInfo(None, None).text(), "firmware unknown · MAC unknown")
        self.assertTrue(t.supports_live_values((0, 19)))
        self.assertFalse(t.supports_live_values((0, 18, 3)))
        self.assertFalse(t.supports_live_values(None))


class TestRequests(unittest.TestCase):
    def test_only_takes_error_replies_for_the_command_it_sent(self):
        # An error for SET_PIN_CONFIG (another request), then the loopback reply.
        board = FakeBoard([reply(0x7F, [0x01, 0x04]), reply(0x4D, [1])])
        self.assertTrue(t.set_midi_loopback(board, True))
        own_error = FakeBoard([reply(0x7F, [0x0D, 0x06])])
        self.assertFalse(t.set_midi_loopback(own_error, True))

    def test_reads_mac_and_version_from_a_current_board(self):
        board = FakeBoard([[sx.MANUFACTURER_ID, 0, 19, 0x46, *MAC_NIBBLES], [sx.MANUFACTURER_ID, 0, 19, 0x4A, 0, 19, 0]])
        self.assertEqual(t.read_board_info(board), t.BoardInfo(MAC, (0, 19, 0)))

    def test_version_from_mac_reply_when_get_version_is_unknown(self):
        board = FakeBoard([[sx.MANUFACTURER_ID, 0, 12, 0x06, *MAC_NIBBLES], [sx.MANUFACTURER_ID, 0, 12, 0x7F, 0x0A, 0x02]])
        self.assertEqual(t.read_board_info(board), t.BoardInfo(MAC, (0, 12)))

    def test_board_that_never_answers(self):
        self.assertEqual(t.read_board_info(FakeBoard([])), t.BoardInfo(None, None))

    def test_ignores_another_boards_or_garbled_reply(self):
        board = FakeBoard(
            [
                [0x7E, 0, 19, 0x46, *MAC_NIBBLES],
                [sx.MANUFACTURER_ID, 0, 19, 0x46, 1, 2, 3],
                [sx.MANUFACTURER_ID, 0, 19, 0x46, *MAC_NIBBLES],
                [sx.MANUFACTURER_ID, 0, 19, 0x4A, 0, 19, 0],
            ]
        )
        self.assertEqual(t.read_board_info(board).mac, MAC)


def pin_ack(pin: int) -> list[int]:
    return reply(0x41, [pin, 0, 0, 1, 0x58, pin, 0, 127])


DEFAULT_ANSWERS = [reply(0x49, []), pin_ack(16), pin_ack(17), pin_ack(15), reply(0x4D, [1]), reply(0x47, [1])]


class TestDefaultConfig(unittest.TestCase):
    def test_reset_buttons_led_on_cc16_loopback_and_dongle_peer(self):
        board = FakeBoard(DEFAULT_ANSWERS)
        lines = t.write_default_config(board, 15, MAC)
        self.assertTrue(all(ok for _, ok in lines))
        self.assertEqual([data[3] for data in board.sent], [0x09, 0x01, 0x01, 0x01, 0x0D, 0x07])
        self.assertEqual(board.sent[1], t.button_config(16))
        self.assertEqual(board.sent[2], t.button_config(17))
        # pin 15, digital out, CC (0xB0 / 2) 16, values 0-127
        self.assertEqual(payload(board.sent[3]), [15, 0x01, 0, 1, 0x58, 16, 0, 127])
        self.assertEqual(payload(board.sent[4]), [1])
        self.assertEqual(payload(board.sent[5]), MAC_NIBBLES)

    def test_pairs_no_dongle_when_its_mac_is_unknown(self):
        board = FakeBoard(DEFAULT_ANSWERS)
        self.assertEqual(len(t.write_default_config(board, 15, None)), 5)
        self.assertNotIn(0x07, [data[3] for data in board.sent])

    def test_uses_the_chosen_led_pin(self):
        board = FakeBoard([*DEFAULT_ANSWERS[:3], pin_ack(21), *DEFAULT_ANSWERS[4:]])
        t.write_default_config(board, 21, None)
        self.assertEqual(payload(board.sent[3])[0], 21)

    def test_reports_each_unacknowledged_step_and_still_writes_the_rest(self):
        board = FakeBoard([a for i, a in enumerate(DEFAULT_ANSWERS) if i not in (0, 4)])
        lines = t.write_default_config(board, 15, MAC)
        self.assertEqual([text for text, ok in lines if not ok], ["reset: no pins, no peers", "MIDI loopback on"])
        self.assertEqual(len(board.sent), 6)

    def test_already_paired_dongle_is_success(self):
        board = FakeBoard([*DEFAULT_ANSWERS[:5], reply(0x7F, [0x07, 0x09])])
        self.assertEqual(t.write_default_config(board, 15, MAC)[-1], ("add dongle 84:F7:03:F2:54:06 as peer", True))


def run(state: t.TesterState, *actions) -> t.TesterState:
    for action in actions:
        if action[0] == "loopback":
            state.set_loopback(action[1])
        elif action[0] == "lit":
            state.lit = action[1]
        else:
            state.midi(action[0], list(action[1:]))
    return state


def start() -> t.TesterState:
    return t.TesterState(15, [1, 2])


class TestState(unittest.TestCase):
    def test_marks_a_button_press_per_path(self):
        state = run(start(), ("usb", 0xB0, 17, 127), ("wireless", 0xB0, 17, 127))
        self.assertEqual(state.buttons[(17, 0xB0)], {"usb", "wireless"})
        self.assertTrue(state.pressed[17])

    def test_pin_16_moves_to_the_next_type_after_press_and_release_over_usb(self):
        state = run(start(), ("usb", 0x90, 16, 127), ("usb", 0x90, 16, 0))
        self.assertEqual(state.type_index, 1)
        self.assertIn("usb", state.buttons[(16, 0x90)])
        self.assertEqual(state.cycle_config(), (t.button_config(16, sx.MIDI_NOTE_OFF), "Note off"))

    def test_ignores_the_release_a_fresh_pin_config_reports(self):
        state = run(start(), ("usb", 0x90, 16, 127), ("usb", 0x90, 16, 0), ("usb", 0x80, 16, 0))
        self.assertEqual(state.type_index, 1)

    def test_no_advance_on_wireless_messages_or_other_types(self):
        state = run(
            start(),
            ("wireless", 0x90, 16, 127),
            ("wireless", 0x90, 16, 0),
            ("usb", 0xB0, 16, 127),
            ("usb", 0xB0, 16, 0),
        )
        self.assertEqual(state.type_index, 0)

    def test_buttons_pass_once_every_row_arrived_on_every_path(self):
        state = start()
        for pin, status, _ in t.ROWS:
            for path in t.PATHS:
                state.midi(path, [0xE0, 0x7F, 0x7F] if status == 0xE0 else [status, pin, 127])
        self.assertTrue(t.buttons_passed(state, ["usb", "wireless"]))
        self.assertFalse(t.buttons_passed(start(), ["usb"]))

    def test_loopback_note_needs_a_press_before_the_release(self):
        state = run(start(), ("loopback", True), ("usb", 0x90, 15, 0))
        self.assertFalse(state.loop_note.released)
        self.assertEqual(state.cycle_config(), (t.button_config(16, sx.MIDI_NOTE_ON, 15), "note 15"))
        run(state, ("usb", 0x90, 15, 127), ("wireless", 0x90, 15, 127), ("usb", 0x90, 15, 0), ("lit", True))
        self.assertEqual(state.loop_note, t.LoopNote(usb=True, wireless=True, held=True, released=True))
        self.assertTrue(t.loopback_passed(state, ["usb", "wireless"]))

    def test_leaves_buttons_alone_while_loopback_is_on(self):
        state = run(start(), ("loopback", True), ("usb", 0x90, 16, 127), ("usb", 0x90, 16, 0))
        self.assertEqual(state.type_index, 0)
        self.assertEqual(state.buttons[(16, 0x90)], set())

    def test_marks_touches_per_pad(self):
        state = run(start(), ("usb", 0xB0, 1, 127), ("wireless", 0xB0, 1, 127), ("usb", 0xB0, 2, 127))
        self.assertTrue(t.touch_passed(state, ["usb"]))
        self.assertFalse(t.touch_passed(state, ["usb", "wireless"]))


if __name__ == "__main__":
    unittest.main()
