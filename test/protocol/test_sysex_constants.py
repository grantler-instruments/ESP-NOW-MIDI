"""CI: enomik_sysex_codec.h must stay in sync with scripts/wizard/enomik_sysex.py."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "scripts"))

from check_sysex_constants import check_sysex_constants, collect_mismatches  # noqa: E402


class TestSysExConstantSync(unittest.TestCase):
    def test_cpp_python_constants_match(self):
        mismatches = collect_mismatches()
        if mismatches:
            self.fail(
                "SysEx constant mismatch:\n" + "\n".join(f"  - {m}" for m in mismatches)
            )

    def test_check_helper_raises_on_drift(self):
        # Sanity: the public helper matches collect_mismatches when in sync.
        try:
            check_sysex_constants()
        except AssertionError as err:
            self.fail(str(err))


if __name__ == "__main__":
    unittest.main()
