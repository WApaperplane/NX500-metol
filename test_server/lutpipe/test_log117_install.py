"""Host filesystem tests using a verified bundle, never a camera.

LOG117_BUNDLE must point at build_bundle.py output.
"""
import os
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

INSTALL = Path(__file__).resolve().parents[2] / "scripts/gamma-log117-install.sh"
SPEC = importlib.util.spec_from_file_location("extension_installer", INSTALL.parents[1] / "tools/extensions/installer.py")
EXTENSIONS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(EXTENSIONS)


@unittest.skipUnless(os.environ.get("LOG117_BUNDLE"), "requires verified release bundle")
class InstallTest(unittest.TestCase):
    def test_install_verify_busy_and_corrupt_source(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            bundle = Path(os.environ["LOG117_BUNDLE"])
            sd = root / "sd"
            sd.mkdir()
            staged = EXTENSIONS.stage(bundle / "manifest.json", bundle, sd, "TEST-VOLUME",
                                      provider=lambda _: "TEST-VOLUME")
            self.assertFalse(staged["camera_install_requested"])
            source = sd / ".nxwa-extensions" / staged["request_id"] / "payload"
            version = root / "version"
            version.write_text("1.12\nNX500\n")
            target = root / "gamma"
            target.mkdir()
            (target / "state").mkdir()
            (target / "state" / "user").write_text("preserve")
            run = root / "run"
            run.mkdir()
            env = dict(os.environ, GAMMA_SRC=str(source), GAMMA_DST=str(target),
                       GAMMA_VERSION_FILE=str(version), GAMMA_RUN=str(run))

            def call(mode):
                return subprocess.run(["sh", str(INSTALL), mode], env=env,
                                      text=True, capture_output=True)
            installed = call("install")
            self.assertEqual(installed.returncode, 0, installed.stdout + installed.stderr)
            self.assertEqual((target / "state/user").read_text(), "preserve")
            self.assertIn("ACTIVATED=false", installed.stdout)
            self.assertEqual(call("check").returncode, 0)
            self.assertIn("RESULT=ALREADY_INSTALLED", call("install").stdout)
            self.assertEqual(len(list(root.glob("gamma-before-log117-*"))), 1)
            (run / "clean-log.policy").touch()
            self.assertIn("ACTIVE_GAMMA_SESSION", call("install").stdout)
            (run / "clean-log.policy").unlink()
            (source / "gamma-reload.sh").write_text("corrupt")
            self.assertIn("SOURCE_VERIFY_FAILED", call("install").stdout)
            self.assertEqual(call("check").returncode, 0)
            version.write_text("1.41\nNX1\n")
            self.assertIn("UNSUPPORTED_FIRMWARE", call("install").stdout)


if __name__ == "__main__":
    unittest.main()
