"""Prepare-only installer with real pinned inputs in a temporary fake root."""
import os
from pathlib import Path
import subprocess
import tarfile
import tempfile
import unittest
import shutil


@unittest.skipUnless(os.environ.get("EYE_PREPARE_DIR"), "requires generated prepare package")
class PrepareTest(unittest.TestCase):
    def run_fixture(self, fail_wants=False):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp) / "root"
            pkg = Path(temp) / "pkg"
            pkg.mkdir()
            with tarfile.open(Path(os.environ["EYE_PREPARE_DIR"]) / "prepare.tar") as t:
                t.extractall(pkg, filter="data")
            with tarfile.open(os.environ["EYE_CONFIG_BACKUP"]) as t:
                for name in ("usr/lib/systemd/system/di-camera-app.service", "opt/usr/nx-ks/dicam/t2_premount.sh", "opt/usr/nx-ks/dicam/di-camera-app.c2"):
                    dest = root / name
                    dest.parent.mkdir(parents=True, exist_ok=True)
                    dest.write_bytes(t.extractfile(name).read())
            for name, data in {"etc/version.info": "1.12\nNX500\n", "sys/class/extcon/USB/state": "0\n", "proc/swaps": "Filename Type Size Used Priority\n", "proc/sys/kernel/random/boot_id": "test-boot\n"}.items():
                dest = root / name
                dest.parent.mkdir(parents=True, exist_ok=True)
                dest.write_text(data)
            fakebin = Path(temp) / "bin"
            fakebin.mkdir()
            bb = root / "opt/usr/nx-ks/busybox"
            bb.write_text('#!/bin/sh\ncmd=$1;shift;exec "$cmd" "$@"\n')
            bb.chmod(0o755)
            mount = fakebin / "mount"
            mount.write_text('#!/bin/sh\nexit 0\n')
            mount.chmod(0o755)
            ctl = fakebin / "systemctl"
            ctl.write_text('#!/bin/sh\ncase "$*" in\n*ExecStart*) echo "ExecStart={ argv[]=/bin/sh /usr/local/lib/nx-eye-af/launch.sh ; }";;\n*Wants*) if [ "$TEST_FAIL_WANTS" = 1 ]; then echo Wants=; else echo Wants=nx-eye-resume.service;fi;;\nesac\n')
            ctl.chmod(0o755)
            ctl.write_text(ctl.read_text().replace('/usr/local', str(root) + '/usr/local'))
            script = (pkg / "prepare.sh").read_text()
            # Redirect only device namespaces, preserving /bin/sh and host tools.
            for prefix in ("/usr/local", "/usr/lib/systemd", "/opt/usr", "/etc/systemd", "/etc/version.info", "/sys/class", "/proc/"):
                script = script.replace(prefix, str(root) + prefix)
            (pkg / "prepare.sh").write_text(script)
            env = dict(os.environ, PATH=str(fakebin) + ":" + os.environ["PATH"], TEST_FAIL_WANTS="1" if fail_wants else "0")
            checked = subprocess.run(["sh", str(pkg / "prepare.sh"), "check"], env=env, capture_output=True, text=True)
            self.assertEqual(checked.returncode, 0, checked.stdout + checked.stderr)
            result = subprocess.run(["sh", str(pkg / "prepare.sh"), "install"], env=env, capture_output=True, text=True)
            if fail_wants:
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse((root / "usr/local/lib/nx-eye-af").exists())
                self.assertFalse((root / "etc/systemd/system/di-camera-app.service.d").exists())
            else:
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn("PREPARED_NOT_ARMED", result.stdout)
                self.assertFalse((root / "opt/usr/nx-eye-af/armed").exists())
                again = subprocess.run(["sh", str(pkg / "prepare.sh"), "install"], env=env, capture_output=True, text=True)
                self.assertNotEqual(again.returncode, 0)
                backup = next(line[7:] for line in result.stdout.splitlines() if line.startswith("BACKUP="))
                withdrawn = subprocess.run(["sh", str(pkg / "prepare.sh"), "rollback", backup], env=env, capture_output=True, text=True)
                self.assertEqual(withdrawn.returncode, 0, withdrawn.stdout + withdrawn.stderr)
                self.assertFalse((root / "usr/local/lib/nx-eye-af").exists())
                self.assertTrue((Path(backup) / "runtime.withdrawn").exists())

    def test_prepare(self):
        self.run_fixture()

    def test_rollback_on_service_mismatch(self):
        self.run_fixture(True)


if __name__ == "__main__":
    unittest.main()
