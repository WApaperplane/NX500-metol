from pathlib import Path
import subprocess
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[2] / "contrib/nx500-eye-af/capture/wait-wireless.sh"


class WirelessTest(unittest.TestCase):
    def test_requires_both_interface_down_and_rfkill_blocked(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            interface = root / "sys/class/net/mlan0"
            radio = root / "sys/class/rfkill/rfkill1"
            interface.mkdir(parents=True)
            radio.mkdir(parents=True)
            (radio / "type").write_text("wlan\n")

            def check(flags, soft):
                (interface / "flags").write_text(flags + "\n")
                (radio / "soft").write_text(soft + "\n")
                return subprocess.run(["sh", str(SCRIPT), "--check-wireless-off", str(root)]).returncode

            self.assertNotEqual(check("0x1003", "0"), 0)
            self.assertNotEqual(check("0x1002", "0"), 0)
            self.assertNotEqual(check("0x1003", "1"), 0)
            self.assertEqual(check("0x1002", "1"), 0)


if __name__ == "__main__":
    unittest.main()
