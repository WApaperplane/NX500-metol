"""Replay the patched OEM hibernation writer.

The point of this test is the failure path: a rejecting gate must hand the boot
back to the stock camera (exit 0, /tmp/.hib_ready) instead of stranding it on a
black screen with zram still swapped and no readiness marker.
"""
import io
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/extensions"))
import build_eye_capture  # noqa: E402

EVIDENCE = next((p for p in (Path(os.environ["R1_OEM_ARCHIVE"]) if os.environ.get("R1_OEM_ARCHIVE") else None,
                             ROOT / "rea-evidence/20261126-eye-log117/startup-before.tar",
                             ROOT.parent / "rea-evidence/20261126-eye-log117/startup-before.tar")
                 if p and p.is_file()), None)
WRITERS = ("usr/bin/hibernation.sh", "usr/bin/make_snapshot.sh")


def writer_fixture(destination):
    with tarfile.open(EVIDENCE) as source, tarfile.open(destination, "w") as out:
        for name in WRITERS:
            data = source.extractfile(name).read()
            info = tarfile.TarInfo(name)
            info.size, info.mode = len(data), 0o755
            out.addfile(info, io.BytesIO(data))


class Replay:
    """A fake root holding only the paths the patched writer touches."""

    def __init__(self, base, script):
        self.root = Path(base) / "root"
        self.root.mkdir()
        self.log = []
        for name in ("tmp", "etc", "dev", "sys/class/leds/card", "sys/power", "sys/block/zram0", "stub"):
            (self.root / name).mkdir(parents=True, exist_ok=True)
        (self.root / "etc/snapshot").write_text("1\n")
        (self.root / "sys/power/bootmode").write_text("1\n")
        (self.root / "sys/block/zram0/disksize").write_text("")
        (self.root / "sys/class/leds/card/trigger").write_text("")
        (self.root / "dev/zero").write_bytes(b"\0" * 512)
        for tool in ("mkswap", "swapon", "swapoff", "mount"):
            stub = self.root / "stub" / tool
            stub.write_text(f'#!/bin/sh\necho "{tool} $*" >> "{self.root}/calls.log"\nexit 0\n')
            stub.chmod(0o755)
        guard = self.root / "stub" / "guard"
        guard.write_text(
            '#!/bin/sh\n'
            f'echo "guard $*" >> "{self.root}/calls.log"\n'
            f'verdict=$(cat "{self.root}/verdict.$(echo "$1" | tr -d \'-\')")\n'
            'exit "${verdict:-0}"\n')
        guard.chmod(0o755)
        sample = self.sandbox(script)
        self.script = self.root / "hibernation.sh"
        self.script.write_text(sample)

    def sandbox(self, text):
        root = str(self.root)
        pairs = (
            ("/bin/sh /usr/local/lib/nx-eye-af/verify-before-capture.sh", root + "/stub/guard"),
            ("/sys/class/leds/card/trigger", root + "/sys/class/leds/card/trigger"),
            ("/sys/block/zram0/disksize", root + "/sys/block/zram0/disksize"),
            ("/dev/mmcblk0p9", root + "/dev/mmcblk0p9"),
            ("/dev/zram0", root + "/dev/zram0"),
            ("/dev/zero", root + "/dev/zero"),
            ("/tmp/.hib_ready", root + "/tmp/.hib_ready"),
            ("/etc/snapshot", root + "/etc/snapshot"),
            ("/sys/power/bootmode", root + "/sys/power/bootmode"),
        )
        for original, replacement in pairs:
            text = text.replace(original, replacement)
        return text

    def verdicts(self, preparation, image_write):
        (self.root / "verdict.atpreparation").write_text(str(preparation))
        (self.root / "verdict.atimagewrite").write_text(str(image_write))

    def run(self):
        env = dict(os.environ)
        env["PATH"] = str(self.root / "stub") + os.pathsep + env.get("PATH", "")
        return subprocess.run(["sh", str(self.script)], capture_output=True, text=True, env=env)

    def calls(self):
        path = self.root / "calls.log"
        return path.read_text().splitlines() if path.exists() else []

    def snapshot_device(self):
        return self.root / "dev/mmcblk0p9"

    def led(self):
        return (self.root / "sys/class/leds/card/trigger").read_text().strip()

    def ready(self):
        return (self.root / "tmp/.hib_ready").exists()


@unittest.skipUnless(EVIDENCE.is_file(), "requires the stored OEM startup archive")
class HibernationHandoffTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        base = Path(cls.temp.name)
        tar = base / "writers.tar"
        writer_fixture(tar)
        cls.out = base / "guards"
        cls.files = build_eye_capture.build(tar, cls.out)
        cls.script = cls.files["hibernation.sh"].decode("ascii")

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def replay(self, preparation, image_write):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        fake = Replay(temp.name, self.script)
        fake.verdicts(preparation, image_write)
        return fake, fake.run()

    def test_gate_rejecting_before_zram_hands_boot_back(self):
        fake, done = self.replay(1, 0)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertTrue(fake.ready(), "no readiness marker for the stock camera")
        self.assertFalse(fake.snapshot_device().exists(), "image header was touched")
        self.assertEqual(fake.led(), "", "card LED lit on a rejected capture")
        calls = fake.calls()
        self.assertIn("guard --at-preparation", calls)
        self.assertNotIn("guard --at-image-write", calls)
        self.assertFalse([c for c in calls if c.startswith(("mkswap", "swapon"))], calls)

    def test_gate_rejecting_at_image_write_releases_zram(self):
        fake, done = self.replay(0, 1)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertTrue(fake.ready(), "no readiness marker for the stock camera")
        self.assertFalse(fake.snapshot_device().exists(), "image header was erased anyway")
        self.assertEqual(fake.led(), "", "card LED lit although the write was refused")
        calls = fake.calls()
        self.assertTrue([c for c in calls if c.startswith("swapon")], calls)
        self.assertTrue([c for c in calls if c.startswith("swapoff")], calls)
        self.assertLess(next(i for i, c in enumerate(calls) if c.startswith("swapon")),
                        next(i for i, c in enumerate(calls) if c.startswith("swapoff")))

    def test_full_pass_writes_header_after_gate(self):
        fake, done = self.replay(0, 0)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertTrue(fake.ready())
        self.assertEqual(fake.led(), "timer")
        self.assertEqual(fake.snapshot_device().stat().st_size, 512)
        calls = fake.calls()
        self.assertEqual([c for c in calls if c.startswith("guard")],
                         ["guard --at-preparation", "guard --at-image-write"])

    def test_patch_shape_is_stable(self):
        text = self.script
        self.assertEqual(text.count("--at-preparation"), 1)
        self.assertEqual(text.count("--at-image-write"), 1)
        self.assertEqual(text.count("echo timer > "), 1)
        self.assertLess(text.index("--at-preparation"), text.index("mkswap /dev/zram0"))
        self.assertLess(text.index("--at-image-write"), text.index("echo timer > "))

    def test_rescue_template_carries_built_hashes(self):
        rescue = self.files["arm-rescue.sh"].decode("utf-8")
        self.assertNotIn("@GUARD@", rescue)
        self.assertIn(build_eye_capture.hashlib.sha256(self.files["hibernation.sh"]).hexdigest(), rescue)
        self.assertIn(build_eye_capture.hashlib.sha256(self.files["verify-before-capture.sh"]).hexdigest(), rescue)


if __name__ == "__main__":
    unittest.main()
