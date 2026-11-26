"""Lifecycle replay: r2 on the body -> withdraw -> install r4 -> withdraw.

The body still carries the r2 header writer. Two properties matter and both
have failed before:

* ``withdraw-guards.sh`` must accept the r2 header *and* this build's header,
  otherwise the only script that can uninstall the guard refuses to run.
* ``install-guards.sh`` must survive a directory left behind by withdraw,
  otherwise one withdraw locks the body out of every later install.
"""
import os
from pathlib import Path
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


GUARDS = first(Path(os.environ.get("EYE_GUARDS_R4") or "/nonexistent"),
               ROOT / "_deliverables/eye-capture-guards-r4-20261126",
               ROOT.parent / "_deliverables/eye-capture-guards-r4-20261126")
BACKUP = first(Path(os.environ.get("R1_OEM_ARCHIVE") or "/nonexistent"),
               ROOT / "rea-evidence/20261126-eye-log117/startup-before.tar",
               ROOT.parent / "rea-evidence/20261126-eye-log117/startup-before.tar")

HEADER = "usr/bin/hibernation.sh"
FACTORY = "usr/bin/make_snapshot.sh"


def relocate(script, root):
    for prefix in ("/usr/local/lib", "/opt/usr/nx-eye-af", "/opt/usr/nx-ks",
                   "/usr/bin/hibernation", "/usr/bin/make_snapshot",
                   "/etc/systemd"):
        script = script.replace(prefix, str(root) + prefix)
    script = script.replace('$(dirname "$0")', str(GUARDS))
    return script


def pin_override(script, real, fake):
    return script.replace(real, fake)


class LifecycleTest(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = root = Path(temp.name)
        with tarfile.open(BACKUP) as archive:
            self.stock = {}
            for name in (HEADER, FACTORY):
                data = archive.extractfile(name).read()
                self.stock[name] = data
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
        (root / "opt/usr/nx-eye-af").mkdir(parents=True)
        rt = root / "usr/local/lib/nx-eye-af"
        rt.mkdir(parents=True)
        (rt / "verify-payload.sh").write_text("#!/bin/sh\nexit 0\n")
        bb = root / "opt/usr/nx-ks/busybox"
        bb.parent.mkdir(parents=True)
        bb.write_text('#!/bin/sh\ncmd=$1;shift;exec "$cmd" "$@"\n')
        bb.chmod(0o755)
        (root / "etc/systemd/system").mkdir(parents=True)
        fakebin = root / "fakebin"
        fakebin.mkdir()
        for name, text in {
            "mount": "exit 0",
            "systemctl": 'case "$*" in *Wants*) echo Wants=nx-eye-resume.service;; esac',
            "sync": "exit 0",
        }.items():
            f = fakebin / name
            f.write_text("#!/bin/sh\n" + text + "\n")
            f.chmod(0o755)
        self.env = {**os.environ, "PATH": str(fakebin) + ":" + os.environ["PATH"]}

    def run_script(self, name, extra=""):
        source = (GUARDS / name).read_text()
        script = relocate(source, self.root)
        path = self.root / ("run-" + name)
        path.write_text(script)
        result = subprocess.run(["sh", str(path)], env=self.env, capture_output=True, text=True)
        return result.returncode, result.stdout + result.stderr

    def write_script(self, name, text):
        path = self.root / name
        path.write_text(text)
        return path

    def test_r4_install_then_withdraw_then_reinstall(self):
        code, out = self.run_script("install-guards.sh")
        self.assertEqual(code, 0, out)
        self.assertIn("GUARDS_INSTALLED_NOT_ARMED", out)
        self.assertEqual((self.root / HEADER).read_bytes(),
                         (GUARDS / "hibernation.sh").read_bytes())

        code, out = self.run_script("withdraw-guards.sh")
        self.assertEqual(code, 0, out)
        self.assertIn("GUARDS_WITHDRAWN_SNAPSHOT_UNCHANGED", out)
        self.assertIn("WRITER_WITHDRAWN=r4", out)
        self.assertEqual((self.root / HEADER).read_bytes(), self.stock[HEADER])
        self.assertEqual((self.root / FACTORY).read_bytes(), self.stock[FACTORY])
        self.assertFalse((self.root / "usr/local/lib/nx-eye-af/verify-before-capture.sh").exists())

        # The archived backup must not block the second install.
        code, out = self.run_script("install-guards.sh")
        self.assertEqual(code, 0, out)
        self.assertIn("GUARDS_INSTALLED_NOT_ARMED", out)
        archived = list((self.root / "opt/usr/nx-eye-af").glob("capture-writers-before.archived-*"))
        self.assertEqual(len(archived), 1, [p.name for p in archived])
        self.assertIn("phase=GUARDS_WITHDRAWN", (archived[0] / "status").read_text())

    def test_legacy_r2_header_is_withdrawable(self):
        # The body must reach this branch with the r2 writer in place. The real
        # r2 bytes are not in the evidence archive, so the placeholder carries
        # their hash instead: only the branch selection is under test here.
        code, out = self.run_script("install-guards.sh")
        self.assertEqual(code, 0, out)
        backup = self.root / "opt/usr/nx-eye-af/capture-writers-before"
        self.assertTrue((backup / "hibernation.sh").exists())

        import hashlib
        marked = self.root / HEADER
        marked.write_bytes(b"legacy-r2-header\n")
        legacy = hashlib.sha256(marked.read_bytes()).hexdigest()
        source = (GUARDS / "withdraw-guards.sh").read_text()
        source = source.replace(
            "1e2bcf8383c0193c9bbe5f21b882d8bef14a765897bf50fc01c5b49447296ab2", legacy)
        code, out = self.run_script_text("withdraw-legacy.sh", source)
        self.assertEqual(code, 0, out)
        self.assertIn("WRITER_WITHDRAWN=r2", out)
        self.assertEqual((self.root / HEADER).read_bytes(), self.stock[HEADER])

    def run_script_text(self, name, source):
        path = self.write_script(name, relocate(source, self.root))
        result = subprocess.run(["sh", str(path)], env=self.env, capture_output=True, text=True)
        return result.returncode, result.stdout + result.stderr


if __name__ == "__main__":
    unittest.main()
