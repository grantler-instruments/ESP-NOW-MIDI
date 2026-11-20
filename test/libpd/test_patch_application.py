"""Verify clean application, repeat configuration, and explicit failure modes."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
PD = ROOT / "components/libpd/libpd/pure-data"
APPLY = ROOT / "components/libpd/cmake/ApplyPatches.cmake"


@unittest.skipUnless((PD / "src/m_pd.h").exists(), "Fetch libpd before running patch tests")
class PatchApplication(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.source = Path(self.tmp.name) / "pure-data"
        subprocess.run(["git", "clone", "--shared", "--no-checkout", str(PD), str(self.source)], check=True, capture_output=True)
        subprocess.run(["git", "-C", str(self.source), "checkout", "--detach", "f009fd8d7b537e209e09898d487fdf1bf547da2b"], check=True, capture_output=True)

    def apply(self):
        return subprocess.run(["cmake", f"-DPD_SOURCE_DIR={self.source}", "-P", str(APPLY)], capture_output=True, text=True)

    def test_apply_and_repeat(self):
        first = self.apply()
        self.assertEqual(first.returncode, 0, first.stdout+first.stderr)
        before = subprocess.check_output(["git", "-C", str(self.source), "diff"])
        self.assertTrue(before)
        second = self.apply()
        self.assertEqual(second.returncode, 0, second.stdout+second.stderr)
        self.assertEqual(second.stdout.count("already applied:"), 4)
        self.assertEqual(before, subprocess.check_output(["git", "-C", str(self.source), "diff"]))

    def test_conflicting_edit_fails(self):
        path = self.source / "src/d_math.c"
        path.write_text(path.read_text().replace("static t_int *mtof_tilde_perform(t_int *w)", "static t_int *renamed_mtof(t_int *w)"))
        result = self.apply()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("does not match the source", result.stderr)
        self.assertIn("renamed_mtof", path.read_text())

    def test_wrong_revision_fails(self):
        subprocess.run(["git", "-C", str(self.source), "-c", "user.name=Test", "-c", "user.email=test@example.invalid", "commit", "--allow-empty", "-m", "Different revision"], check=True, capture_output=True)
        result = self.apply()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("require Pd 0.56-5", result.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
