"""Port selection regressions; stub the MIDI backend so no hardware is needed."""

import importlib.util
import sys
import unittest
from pathlib import Path
from types import ModuleType
from unittest.mock import Mock, patch


path = Path(__file__).resolve().parents[2] / "scripts/wizard/midi_io.py"
spec = importlib.util.spec_from_file_location("wizard_midi_io", path)
midi_io = importlib.util.module_from_spec(spec)
with patch.dict(sys.modules, {spec.name: midi_io, "mido": ModuleType("mido")}):
    spec.loader.exec_module(midi_io)


class TestWizardPortSelection(unittest.TestCase):
    def setUp(self):
        self.backend = Mock()
        self.backend_patch = patch.object(midi_io, "mido", self.backend)
        self.backend_patch.start()
        self.addCleanup(self.backend_patch.stop)

    def ports(self, inputs, outputs=None):
        self.backend.get_input_names.return_value = inputs
        self.backend.get_output_names.return_value = inputs if outputs is None else outputs

    def test_missing_dongle_never_opens_client_automatically(self):
        self.ports(["enomik3000_client"])
        with self.assertRaisesRegex(RuntimeError, "No MIDI ports found for dongle"):
            midi_io.find_link(midi_io.DONGLE_PORT_NAMES, "dongle", allow_prompt=False)
        self.backend.open_input.assert_not_called()
        self.backend.open_output.assert_not_called()

    def test_missing_client_never_opens_dongle_automatically(self):
        self.ports(["enomik3000_dongle"])
        with self.assertRaisesRegex(RuntimeError, "No MIDI ports found for client"):
            midi_io.find_link(midi_io.CLIENT_PORT_NAMES, "client", allow_prompt=False)
        self.backend.open_input.assert_not_called()
        self.backend.open_output.assert_not_called()

    def test_each_role_opens_its_own_input_and_output(self):
        self.ports(["enomik3000_client", "enomik3000_dongle"])
        for names, role in ((midi_io.CLIENT_PORT_NAMES, "client"), (midi_io.DONGLE_PORT_NAMES, "dongle")):
            with self.subTest(role=role):
                link = midi_io.find_link(names, role, allow_prompt=False)
                expected = "enomik3000_" + role
                self.assertEqual(link.name, expected)
                self.backend.open_input.assert_called_with(expected)
                self.backend.open_output.assert_called_with(expected)
                link.close()

    def test_missing_output_does_not_mix_device_roles(self):
        self.ports(["enomik3000_dongle"], ["enomik3000_client"])
        with self.assertRaises(RuntimeError):
            midi_io.find_link(midi_io.DONGLE_PORT_NAMES, "dongle", allow_prompt=False)
        self.backend.open_input.assert_not_called()

    def test_explicit_custom_port_still_works(self):
        self.ports(["Grantler USB MIDI"])
        link = midi_io.find_link(["Grantler"], "dongle", allow_prompt=False)
        self.assertEqual(link.name, "Grantler USB MIDI")
        link.close()
