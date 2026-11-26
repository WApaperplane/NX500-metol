import subprocess
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
GATE = ROOT / "contrib/nx500-eye-af/capture/battery-gate.sh"


class BatteryGateTest(unittest.TestCase):
    def run_gate(self, capacity, confirmed=0, status="Charging", voltage="8400000"):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        sysfs = Path(temp.name)
        for name, value in (("capacity", capacity), ("status", status), ("voltage_now", voltage)):
            if value is not None:
                (sysfs / name).write_text(value + "\n")
        return subprocess.run(["sh", str(GATE), str(sysfs), str(confirmed)], capture_output=True, text=True)

    def test_raw_hardware_value_is_not_a_percentage(self):
        # The body was observed returning 1460 here; it must not pass as 1460%.
        done = self.run_gate("1460")
        self.assertEqual(done.returncode, 3, done.stdout + done.stderr)
        self.assertIn("BATTERY_VALUE_IS_NOT_A_PERCENT", done.stderr)
        self.assertIn("capacity=1460", done.stdout)
        self.assertIn("confirmed_full=0", done.stdout)

    def test_raw_hardware_value_with_operator_claim(self):
        done = self.run_gate("1460", confirmed=1)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn("confirmed_full=1", done.stdout)

    def test_percentage_below_full(self):
        done = self.run_gate("42")
        self.assertEqual(done.returncode, 4)
        self.assertIn("FULL_BATTERY_REQUIRED", done.stderr)

    def test_percentage_at_threshold(self):
        done = self.run_gate("95")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)

    def test_unreadable_needs_claim(self):
        self.assertEqual(self.run_gate(None).returncode, 3)
        self.assertEqual(self.run_gate(None, confirmed=1).returncode, 0)


if __name__ == "__main__":
    unittest.main()
