"""Exercise vlog-arm with a fake camera guard; no camera paths are touched."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[2] / "scripts" / "vlog-arm.sh"
VLOG = SCRIPT.with_name("vlog.sh")


class GuardTest(unittest.TestCase):
    def test_arm_stops_on_guard_failure_and_runs_preflight_on_success(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            ks = root / "ks"
            ks.mkdir()
            busybox = ks / "busybox"
            busybox.write_text('#!/bin/sh\ncmd=$1; shift; exec "$cmd" "$@"\n')
            busybox.chmod(0o755)
            preflight = ks / "preflight.sh"
            preflight.write_text('#!/bin/sh\necho called > "$TEST_MARKER"\n')
            guard = root / "guard"
            guard.write_text('#!/bin/sh\nexit "$TEST_GUARD_RC"\n')
            guard.chmod(0o755)
            script = root / "vlog-arm.sh"
            source = SCRIPT.read_text(encoding="utf-8")
            source = source.replace("BB=/opt/usr/nx-ks/busybox", f"BB={busybox}")
            source = source.replace("KS=/opt/usr/nx-ks", f"KS={ks}")
            script.write_text(source, encoding="utf-8")
            marker = root / "preflight-called"
            env = dict(os.environ, VLOG_GUARD=str(guard),
                       VLOG_PRIV_ROOT=str(root / "private"), TEST_MARKER=str(marker))
            for rc in (1, 2, 3, 4):
                result = subprocess.run(["sh", str(script), "arm"], env={**env, "TEST_GUARD_RC": str(rc)},
                                        capture_output=True, text=True)
                self.assertEqual(result.returncode, rc, result.stdout)
                self.assertIn("RESULT=GUARD_PREFLIGHT_FAILED", result.stdout)
                self.assertFalse(marker.exists())
            result = subprocess.run(["sh", str(script), "arm"], env={**env, "TEST_GUARD_RC": "0"},
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout)
            self.assertTrue(marker.exists())

    def test_video_restore_only_for_factory_and_failure_is_reported(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            ks, run = root / "ks", root / "run"
            (ks / "gamma" / "bin").mkdir(parents=True)
            (run / "session").mkdir(parents=True)
            (run / "session" / "video_enabled").touch()
            bb = ks / "busybox"
            bb.write_text('#!/bin/sh\ncmd=$1; shift; exec "$cmd" "$@"\n')
            bb.chmod(0o755)
            engine = ks / "gamma" / "gamma-reload.sh"
            engine.write_text('#!/bin/sh\nprintf "mode=%s\\n" "$TEST_MODE"\n')
            engine.chmod(0o755)
            writer = ks / "gamma" / "bin" / "nx_regtabwrite.arm"
            writer.write_text('#!/bin/sh\necho called > "$TEST_MARKER"\nexit "$TEST_WRITER_RC"\n')
            writer.chmod(0o755)
            source = VLOG.read_text(encoding="utf-8").replace("BB=/opt/usr/nx-ks/busybox", f"BB={bb}")
            source = source.replace("D=/opt/usr/nx-ks", f"D={ks}").replace("RUN=/tmp/nx-lut-lite", f"RUN={run}")
            source = source.replace("/mnt/mmc/NXDEMO51", str(root / "audit"))
            script = root / "vlog.sh"
            script.write_text(source, encoding="utf-8")
            marker = root / "writer-called"
            env = dict(os.environ, TEST_MARKER=str(marker), TEST_WRITER_RC="1")
            log = subprocess.run(["sh", str(script), "on"], env={**env, "TEST_MODE": "LOG"},
                                 capture_output=True, text=True)
            self.assertEqual(log.returncode, 0, log.stdout)
            self.assertFalse(marker.exists())
            restore = subprocess.run(["sh", str(script), "off"], env={**env, "TEST_MODE": "FACTORY"},
                                     capture_output=True, text=True)
            self.assertNotEqual(restore.returncode, 0)
            self.assertIn("RESULT=PARTIAL_VIDEO_POINTERS_UNCONFIRMED", restore.stdout)
            self.assertTrue(marker.exists())


if __name__ == "__main__":
    unittest.main()
