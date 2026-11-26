import os
from pathlib import Path
import subprocess
import tarfile
import tempfile
import unittest
import hashlib


@unittest.skipUnless(os.environ.get("EYE_GUARDS_DIR"), "requires protected writers")
class GuardInstallTest(unittest.TestCase):
    def fixture(self, fail):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            with tarfile.open(os.environ["EYE_CONFIG_BACKUP"]) as t:
                originals = {}
                for name in ("usr/bin/hibernation.sh", "usr/bin/make_snapshot.sh"):
                    path = root / name
                    path.parent.mkdir(parents=True, exist_ok=True)
                    originals[name] = t.extractfile(name).read()
                    path.write_bytes(originals[name])
            state = root / "opt/usr/nx-eye-af"
            state.mkdir(parents=True)
            bb = root / "opt/usr/nx-ks/busybox"
            bb.parent.mkdir(parents=True)
            bb.write_text('#!/bin/sh\ncmd=$1;shift;exec "$cmd" "$@"\n')
            bb.chmod(0o755)
            rt = root / "usr/local/lib/nx-eye-af"
            rt.mkdir(parents=True)
            (rt / "verify-payload.sh").write_text("#!/bin/sh\nexit 0\n")
            (root / "etc/systemd/system").mkdir(parents=True)
            fakebin = root / "fakebin"
            fakebin.mkdir()
            for name, text in {"mount": "exit 0", "systemctl": "case \"$*\" in *Wants*) echo Wants=" + ("" if fail else "nx-eye-resume.service") + ";; esac"}.items():
                f = fakebin / name
                f.write_text("#!/bin/sh\n" + text + "\n")
                f.chmod(0o755)
            source = Path(os.environ["EYE_GUARDS_DIR"])
            script = (source / "install-guards.sh").read_text()
            for prefix in ("/usr/local", "/opt/usr", "/usr/bin/hibernation", "/usr/bin/make_snapshot", "/etc/systemd"):
                script = script.replace(prefix, str(root) + prefix)
            # The source package remains unchanged, so its table still pins the
            # actual protected writer bytes rather than test-transformed bytes.
            script = script.replace('src=$(CDPATH= cd -- "$(dirname "$0")" && pwd)', 'src=' + str(source))
            test = root / "install.sh"
            test.write_text(script)
            r = subprocess.run(["sh", str(test)], env={**os.environ, "PATH": str(fakebin) + ":" + os.environ["PATH"]}, capture_output=True, text=True)
            if fail:
                self.assertNotEqual(r.returncode, 0)
                for name, data in originals.items():
                    self.assertEqual((root / name).read_bytes(), data)
                self.assertFalse((rt / "verify-before-capture.sh").exists())
            else:
                self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
                self.assertIn("GUARDS_INSTALLED_NOT_ARMED", r.stdout)
                self.assertFalse((state / "armed").exists())

    def test_success(self):
        self.fixture(False)

    def test_automatic_rollback(self):
        self.fixture(True)


if __name__ == "__main__":
    unittest.main()
