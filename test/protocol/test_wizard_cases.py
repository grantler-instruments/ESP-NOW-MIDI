"""Host-side tests for scripts/wizard/cases.py (wizard test cases, no MIDI needed)."""

from __future__ import annotations

import importlib.util
import re
import sys
import unittest
from pathlib import Path
from types import SimpleNamespace

REPO_ROOT = Path(__file__).resolve().parents[2]


def load(name: str):
    path = REPO_ROOT / "scripts" / "wizard" / f"{name}.py"
    spec = importlib.util.spec_from_file_location(f"wizard_{name}", path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    sys.modules[spec.name] = module  # dataclasses look their module up here
    spec.loader.exec_module(module)
    return module


tc = load("cases")
sx = load("enomik_sysex")


def as_message(type_: str, kwargs: dict) -> SimpleNamespace:
    """Stand-in for the mido.Message the wizard would send or receive."""
    return SimpleNamespace(type=type_, **kwargs)


def echo_of(case) -> SimpleNamespace:
    return as_message(case.type, case.message_kwargs())


class TestEchoCases(unittest.TestCase):
    def all_cases(self):
        return tc.channel_voice_cases() + tc.system_cases()

    def test_every_case_matches_its_own_echo(self):
        for case in self.all_cases():
            with self.subTest(case.name):
                self.assertTrue(case.matches(echo_of(case)))

    def test_cases_cover_every_message_type(self):
        types = {case.type for case in self.all_cases()}
        self.assertEqual(
            types,
            {
                "note_on", "note_off", "control_change", "program_change", "pitchwheel",
                "aftertouch", "polytouch", "clock", "start", "continue", "stop",
                "songpos", "song_select",
            },
        )

    def test_case_names_are_unique(self):
        names = [case.name for case in self.all_cases()]
        self.assertEqual(len(names), len(set(names)))

    def test_values_are_in_midi_range(self):
        limits = {"pitch": (-8192, 8191), "pos": (0, 16383)}
        for case in self.all_cases():
            for name, value in case.fields.items():
                low, high = limits.get(name, (0, 127))
                with self.subTest(case.name, field=name):
                    self.assertTrue(low <= value <= high)

    def test_voice_cases_use_the_echo_channel(self):
        for case in tc.channel_voice_cases():
            self.assertEqual(case.message_kwargs()["channel"], tc.ECHO_CHANNEL - 1)

    def test_system_cases_have_no_channel(self):
        for case in tc.system_cases():
            self.assertNotIn("channel", case.message_kwargs())

    def test_rejects_other_channel_type_and_value(self):
        case = tc.channel_voice_cases()[0]
        msg = echo_of(case)
        self.assertFalse(case.matches(as_message(case.type, {**case.message_kwargs(), "channel": 0})))
        self.assertFalse(case.matches(as_message("note_off", case.message_kwargs())))
        self.assertFalse(case.matches(as_message(case.type, {**case.message_kwargs(), "velocity": msg.velocity - 1})))

    def test_tolerance(self):
        case = tc.io_bend(0, 256)
        self.assertTrue(case.matches(as_message("pitchwheel", {"channel": 0, "pitch": 256})))
        self.assertTrue(case.matches(as_message("pitchwheel", {"channel": 0, "pitch": -256})))
        self.assertFalse(case.matches(as_message("pitchwheel", {"channel": 0, "pitch": 257})))


class TestFakeInputs(unittest.TestCase):
    def test_fake_messages_use_the_fake_channel(self):
        for type_, fields in (
            tc.fake_analog(10, 64), tc.fake_press(16), tc.fake_release(16), tc.fake_clear(),
        ):
            self.assertEqual(fields["channel"], tc.FAKE_CHANNEL - 1)

    def test_fake_messages_address_the_pin(self):
        self.assertEqual(tc.fake_analog(10, 64), ("control_change", {"channel": 14, "control": 10, "value": 64}))
        self.assertEqual(tc.fake_press(16)[1]["note"], 16)
        self.assertEqual(tc.fake_release(16)[0], "note_off")

    def test_wizard_channels_are_separate(self):
        channels = {tc.FAKE_CHANNEL, tc.ECHO_CHANNEL, tc.IO_CHANNEL, tc.API_TRIGGER_CHANNEL, tc.API_CHANNEL, 16}
        self.assertEqual(len(channels), 6)

    def test_analog_sweep_always_changes_and_avoids_snapped_levels(self):
        levels = (0,) + tc.ANALOG_SWEEP  # the wizard primes the pin to 0 first
        for before, after in zip(levels, levels[1:]):
            self.assertNotEqual(before, after)
        # 1 and 126 snap to the endpoints in firmware, so they are never sent.
        self.assertFalse({1, 126} & set(tc.ANALOG_SWEEP))

    def test_bend_sweep_covers_both_ends_and_center(self):
        pitches = {pitch for _, pitch, _ in tc.BEND_SWEEP}
        self.assertEqual(pitches, {-8192, 0, 8191})
        self.assertNotEqual(tc.BEND_SWEEP[0][0], 0)  # differs from the primed level

    def test_is_note_release(self):
        self.assertTrue(tc.is_note_release(as_message("note_off", {"channel": 0, "note": 60, "velocity": 0}), 60))
        self.assertTrue(tc.is_note_release(as_message("note_on", {"channel": 0, "note": 60, "velocity": 0}), 60))
        self.assertFalse(tc.is_note_release(as_message("note_on", {"channel": 0, "note": 60, "velocity": 1}), 60))
        self.assertFalse(tc.is_note_release(as_message("note_off", {"channel": 0, "note": 61, "velocity": 0}), 60))

    def test_pins_are_distinct_inputs(self):
        inputs = [tc.PIN_DIGITAL_A, tc.PIN_DIGITAL_B, tc.PIN_ANALOG_IN, tc.PIN_NOTE, tc.PIN_BEND]
        self.assertEqual(len(inputs), len(set(inputs)))


class TestApiCases(unittest.TestCase):
    SKETCHES = (
        REPO_ROOT / "examples" / "client_test" / "client_test.ino",
        REPO_ROOT / "examples_idf" / "client_test" / "main" / "main.cpp",
    )

    def test_every_api_case_matches_its_message(self):
        for case in tc.API_CASES:
            with self.subTest(case.name):
                self.assertTrue(case.matches(echo_of(case)))

    def test_api_trigger(self):
        self.assertEqual(tc.api_trigger(3), ("program_change", {"channel": tc.API_TRIGGER_CHANNEL - 1, "program": 3}))

    def test_only_sysex_is_usb_only(self):
        for case in tc.API_CASES:
            self.assertEqual(case.usb_only, case.type == "sysex", case.name)

    def test_sysex_data_matches(self):
        case = next(c for c in tc.API_CASES if c.name == "sendSysEx long")
        self.assertTrue(case.matches(as_message("sysex", {"data": list(tc.API_SYSEX_LONG)})))
        self.assertFalse(case.matches(as_message("sysex", {"data": list(tc.API_SYSEX_SHORT)})))
        self.assertTrue(all(0 <= b <= 127 for b in tc.API_SYSEX_LONG))

    def test_firmware_handles_every_api_case(self):
        for sketch in self.SKETCHES:
            source = sketch.read_text()
            body = source[source.index("runApiCase(byte index)"):]
            body = body[: body.index("\n}\n")]
            cases = [int(n) for n in re.findall(r"case (\d+):", body)]
            with self.subTest(sketch.name):
                self.assertEqual(cases, list(range(len(tc.API_CASES))))

    def test_firmware_sysex_matches(self):
        for sketch in self.SKETCHES:
            source = sketch.read_text()
            for name, want in (("kSysExShort", tc.API_SYSEX_SHORT), ("kSysExLong", tc.API_SYSEX_LONG)):
                raw = re.search(name + r"\[\] = \{([^}]*)\}", source).group(1)
                data = [int(b, 16) for b in raw.replace("\n", " ").split(",")]
                with self.subTest(sketch.name, array=name):
                    self.assertEqual(data, [0xF0, *want, 0xF7])


class TestBurst(unittest.TestCase):
    def test_burst_notes(self):
        self.assertEqual(tc.burst_notes(3), [0, 1, 2])
        self.assertEqual(len(tc.burst_notes(128)), 128)
        with self.assertRaises(ValueError):
            tc.burst_notes(0)
        with self.assertRaises(ValueError):
            tc.burst_notes(129)

    def test_stats_all_received(self):
        sent = {n: 0.0 for n in range(20)}
        received = {n: (n + 1) / 1000.0 for n in range(20)}
        stats = tc.burst_stats(sent, received)
        self.assertEqual((stats["sent"], stats["received"], stats["lost"]), (20, 20, 0))
        self.assertEqual(stats["loss"], 0.0)
        self.assertAlmostEqual(stats["avg_ms"], 10.5)
        self.assertAlmostEqual(stats["max_ms"], 20.0)
        self.assertAlmostEqual(stats["p95_ms"], 20.0)

    def test_stats_with_loss(self):
        stats = tc.burst_stats({0: 0.0, 1: 0.0, 2: 0.0, 3: 0.0}, {0: 0.002})
        self.assertEqual(stats["lost"], 3)
        self.assertAlmostEqual(stats["loss"], 0.75)
        self.assertAlmostEqual(stats["avg_ms"], 2.0)

    def test_stats_nothing_received(self):
        stats = tc.burst_stats({0: 0.0}, {})
        self.assertEqual(stats["loss"], 1.0)
        self.assertIsNone(stats["avg_ms"])

    def test_stats_ignore_unexpected_notes(self):
        stats = tc.burst_stats({0: 0.0}, {0: 0.001, 5: 0.001})
        self.assertEqual(stats["received"], 1)


class TestWizardSysexAdditions(unittest.TestCase):
    def test_build_raw(self):
        self.assertEqual(
            sx.build_raw(0x3E),
            [sx.MANUFACTURER_ID, sx.PROTOCOL_MAJOR, sx.PROTOCOL_MINOR, 0x3E],
        )
        self.assertEqual(sx.build_raw(sx.CMD_ADD_PEER, [1, 2])[-2:], [1, 2])
        self.assertEqual(sx.build_raw(sx.CMD_GET_VERSION, major=sx.PROTOCOL_MAJOR + 1)[1], sx.PROTOCOL_MAJOR + 1)

    def test_pitch_bend_pin_config_round_trip(self):
        data = sx.build_set_pin_config(
            pin=11, mode=sx.MODE_ANALOG_INPUT, channel=1, midi_type=sx.MIDI_PITCH_BEND, note_or_cc=0
        )
        self.assertTrue(all(0 <= b <= 127 for b in data))
        parsed = sx._parse_pin_config(data[4:])
        self.assertEqual(parsed["midi_type"], sx.MIDI_PITCH_BEND)
        self.assertEqual(parsed["pin"], 11)

    def test_note_pin_config_round_trip(self):
        data = sx.build_set_pin_config(
            pin=18, mode=sx.MODE_INPUT_PULLUP, channel=1, midi_type=sx.MIDI_NOTE_ON, note_or_cc=60
        )
        parsed = sx._parse_pin_config(data[4:])
        self.assertEqual((parsed["midi_type"], parsed["note_or_cc"]), (sx.MIDI_NOTE_ON, 60))


if __name__ == "__main__":
    unittest.main()
