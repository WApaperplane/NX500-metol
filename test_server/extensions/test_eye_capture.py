import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
GUARD = ROOT / "contrib/nx500-eye-af/capture/verify-before-capture.sh"

BEFORE = {"active": "inactive", "sub": "dead", "started": "0", "pid": "0",
          "exec": "{ path=/usr/bin/hibernation.sh ; argv[]=/usr/bin/hibernation.sh ; ignore_errors=no ; }"}
WRITING = {"active": "activating", "sub": "start", "started": "177",
           "exec": "{ path=/usr/bin/hibernation.sh ; argv[]=/usr/bin/hibernation.sh ; ignore_errors=no ; }"}


def build_root(root, writer, wants=True):
    files = {"proc/sys/kernel/random/boot_id": "11111111-1111-1111-1111-111111111111\n",
             "sys/power/bootmode": "1\n", "proc/swaps": "Filename Type Size Used Priority\n",
             "etc/snapshot": "1\n"}
    for name, text in files.items():
        f = root / name
        f.parent.mkdir(parents=True, exist_ok=True)
        f.write_text(text)
    directory = root / "etc/systemd/system/hib_post.service.wants"
    directory.mkdir(parents=True)
    (root / "run").mkdir()
    (directory / "nx-eye-resume.service").symlink_to("/etc/systemd/system/nx-eye-resume.service")
    ctl = root / "usr/bin/systemctl"
    ctl.parent.mkdir(parents=True)
    ctl.write_text(f'''#!/bin/sh
case "$*" in
*list-units*) printf 'hib_post.path loaded\\nhib_post.service loaded\\nnx-eye-resume.service loaded\\n';;
*hibernation.service*) printf 'LoadState=loaded\\nNeedDaemonReload=no\\nActiveState={writer["active"]}\\nSubState={writer["sub"]}\\nExecMainStartTimestampMonotonic={writer["started"]}\\nExecMainPID={writer["pid"]}\\nExecStart={writer["exec"]}\\n';;
*hib_post.path*) printf 'LoadState=loaded\\nNeedDaemonReload=no\\nTriggers=hib_post.service\\n';;
*hib_post.service*) printf 'LoadState=loaded\\nNeedDaemonReload=no\\nTriggeredBy=hib_post.path\\nWants={'nx-eye-resume.service' if wants else ''}\\n';;
*nx-eye-resume.service*) printf 'LoadState=loaded\\nNeedDaemonReload=no\\nAfter=hib_post.service\\nActiveState=inactive\\nExecStart={{ path=/bin/sh ; argv[]=/bin/sh /usr/local/lib/nx-eye-af/wake.sh ; ignore_errors=no ; }}\\n';;
esac
''')
    ctl.chmod(0o755)
    script = root / "guard.sh"
    script.write_text(GUARD.read_text().replace("root=\n", "root=" + str(root) + "\n", 1))
    script.chmod(0o755)
    return script


class CaptureGuardTest(unittest.TestCase):
    def fixture(self, writer, wants=True):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        root = Path(temp.name)
        return build_root(root, writer, wants), root

    def receipt(self, root):
        # Stage modes redirect their own output into /run/nx-eye-capture.
        return "\n".join(p.read_text() for p in sorted((root / "run/nx-eye-capture").glob("*.txt")))

    def test_capture_manager_and_missing_dependency(self):
        script, _ = self.fixture(BEFORE)
        good = subprocess.run(["sh", str(script)], capture_output=True, text=True)
        self.assertEqual(good.returncode, 0, good.stdout + good.stderr)
        self.assertIn("decision=PASS", good.stdout)
        script, _ = self.fixture(BEFORE, wants=False)
        bad = subprocess.run(["sh", str(script)], capture_output=True, text=True)
        self.assertNotEqual(bad.returncode, 0)
        self.assertIn("COMPANION_MISSING_FROM_WANTS", bad.stdout)

    def test_preparation_stage_accepts_the_current_writer(self):
        # Inside hibernation.sh the gate's parent is the unit's main process.
        writer = dict(WRITING, pid=str(os.getpid()))
        script, root = self.fixture(writer)
        done = subprocess.run([str(script), "--at-preparation"], capture_output=True, text=True)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        text = self.receipt(root)
        self.assertIn("stage=image-preparation", text)
        self.assertIn("decision=PASS", text)

    def test_preparation_stage_rejects_a_foreign_writer(self):
        writer = dict(WRITING, pid=str(os.getpid()),
                      exec="{ path=/bin/sh ; argv[]=/bin/sh /tmp/fake.sh ; ignore_errors=no ; }")
        script, root = self.fixture(writer)
        done = subprocess.run([str(script), "--at-preparation"], capture_output=True, text=True)
        self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
        self.assertIn("WRITER_IS_NOT_CURRENT_STOCK_MAIN", self.receipt(root))

    def test_preparation_stage_rejects_a_stale_manager(self):
        writer = dict(WRITING, pid=str(os.getpid()), active="inactive", sub="dead")
        script, root = self.fixture(writer)
        done = subprocess.run([str(script), "--at-preparation"], capture_output=True, text=True)
        self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
        self.assertIn("decision=STOP", self.receipt(root))


if __name__ == "__main__":
    unittest.main()
