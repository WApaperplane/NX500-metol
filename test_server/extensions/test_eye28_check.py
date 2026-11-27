"""Replay the read-only pre-install check for Eye AF 28 on a fake root.

The check exists so a stale unit, a half-removed install or a mutated writer is
caught before anything is copied to the body. Only the pin constants are
rewritten here; the script logic runs unmodified.
"""
import os
from pathlib import Path
import hashlib
import subprocess
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def first(*candidates):
    for path in candidates:
        if path and path.exists():
            return path
    return candidates[0]


SCRIPT = first(ROOT.parent / "_deliverables/eye28-20261127/30-verify/check-before-install.sh",
               ROOT / "_deliverables/eye28-20261127/30-verify/check-before-install.sh")
BACKUP = first(ROOT / "rea-evidence/20261126-eye-log117/startup-before.tar",
               ROOT.parent / "rea-evidence/20261126-eye-log117/startup-before.tar")
WRITERS = ("usr/bin/hibernation.sh", "usr/bin/make_snapshot.sh")


class CheckTest(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = root = Path(temp.name)
        (root / "etc").mkdir(parents=True)
        (root / "etc/version.info").write_text("NX500\n1.12\n")
        self.pins = {}
        for rel in ("usr/lib/systemd/system/di-camera-app.service",
                    "opt/usr/nx-ks/dicam/t2_premount.sh",
                    "opt/usr/nx-ks/dicam/di-camera-app.c2",
                    "usr/apps/com.samsung.di-camera-app/bin/di-camera-app"):
            path = root / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"fixture:" + rel.encode())
            self.pins[rel] = hashlib.sha256(path.read_bytes()).hexdigest()
        with tarfile.open(BACKUP) as archive:
            for rel in WRITERS:
                data = archive.extractfile(rel).read()
                path = root / rel
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
        for rel, text in (("sys/class/extcon/USB/state", "0"),
                          ("sys/power/bootmode", "2"),
                          ("proc/swaps", "Filename\t\t\t\tType\t\tSize\tUsed\tPriority\n")):
            path = root / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text)
        bb = root / "opt/usr/nx-ks/busybox"
        bb.parent.mkdir(parents=True, exist_ok=True)
        bb.write_text('#!/bin/sh\nexec "$@"\n')
        bb.chmod(0o755)

    def run_check(self):
        script = SCRIPT.read_text()
        script = script.replace(
            "ac1bab448ef00c8038104742861d631dc3991d42ef9be575779af72ac778620d",
            self.pins["usr/lib/systemd/system/di-camera-app.service"])
        script = script.replace(
            "99931046311096f82ff58f3d6cdf951baf1053427909d2de582364d9972139c7",
            self.pins["opt/usr/nx-ks/dicam/t2_premount.sh"])
        script = script.replace(
            "ec3951233eb81af69f4461cc2e8c44d247ec03f4467305cd8200b11f7f61bca4",
            self.pins["opt/usr/nx-ks/dicam/di-camera-app.c2"])
        script = script.replace(
            "55dcc78143724435a2e38b9e0d809960bd728668634bbcf7532ce137f882eb04",
            self.pins["usr/apps/com.samsung.di-camera-app/bin/di-camera-app"])
        path = self.root / "check.sh"
        path.write_text(script)
        result = subprocess.run(["sh", str(path), "--root", str(self.root)],
                                capture_output=True, text=True)
        return result.returncode, result.stdout + result.stderr

    def test_clean_body_is_ready(self):
        code, out = self.run_check()
        self.assertEqual(code, 0, out)
        self.assertIn("RESULT=READY_FOR_PREPARE", out)
        self.assertIn("PASS NO_FOOTPRINT", out)
        self.assertIn("PASS HEADER_WRITER", out)

    def test_existing_install_blocks(self):
        (self.root / "opt/usr/nx-eye-af").mkdir(parents=True)
        code, out = self.run_check()
        self.assertNotEqual(code, 0)
        self.assertIn("FAIL NO_FOOTPRINT", out)
        self.assertIn("RESULT=BLOCKED", out)

    def test_mutated_writer_blocks(self):
        path = self.root / "usr/bin/hibernation.sh"
        path.write_bytes(b"tampered\n")
        code, out = self.run_check()
        self.assertNotEqual(code, 0)
        self.assertIn("FAIL HEADER_WRITER", out)

    def test_usb_connected_blocks(self):
        (self.root / "sys/class/extcon/USB/state").write_text("1")
        code, out = self.run_check()
        self.assertNotEqual(code, 0)
        self.assertIn("FAIL USB_CLEAR", out)

    def test_busy_snapshot_blocks(self):
        (self.root / "proc/swaps").write_text(
            "Filename\t\t\t\tType\t\tSize\tUsed\tPriority\n/dev/mmcblk0p9  partition  81920  0  -1\n")
        code, out = self.run_check()
        self.assertNotEqual(code, 0)
        self.assertIn("FAIL SNAPSHOT_IDLE", out)


if __name__ == "__main__":
    unittest.main()
