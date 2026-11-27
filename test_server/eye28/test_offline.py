import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
import zipfile
import shutil
from unittest import mock

ROOT = Path(__file__).resolve().parents[2] / "contrib/nx500-eye28"

def decision(script, facts, env=None):
    return subprocess.check_output(["/bin/sh", str(ROOT.parents[1] / "tests/models" / script), str(facts)], text=True, env=env).strip()

class Offline(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.f = Path(self.temp.name)
        for name, value in {"generation":"g1", "boot_id":"b1", "build_id":"v1", "report_id":"r1", "proof_generation":"g1", "proof_boot_id":"b1", "proof_build_id":"v1", "proof_report_id":"r1", "proof_status":"PASS", "dependency_status":"PASS", "mapping_status":"PASS", "snapshot_generation":"g1", "launch_status":"PASS", "already_started":"NO"}.items(): self.put(name, value)
        (self.f / "library").write_bytes(b"fixture")
        self.put("expected_sha256", hashlib.sha256(b"fixture").hexdigest())
    def put(self, name, value): (self.f / name).write_text(value + "\n")
    def test_positive_and_negative_facts(self):
        self.assertEqual(decision("selector.sh", self.f), "CANDIDATE")
        self.assertEqual(decision("resume.sh", self.f), "ENABLE")
        for name in ("generation", "boot_id", "build_id", "report_id", "snapshot_generation", "proof_status", "dependency_status", "mapping_status", "expected_sha256"):
            original = (self.f/name).read_text()
            self.put(name, "stale")
            self.assertEqual(decision("selector.sh", self.f), "NATIVE", name)
            (self.f/name).write_text(original)
        for name in ("launch_status", "already_started"):
            original=(self.f/name).read_text(); self.put(name, "bad")
            self.assertEqual(decision("resume.sh", self.f), "NATIVE", name)
            (self.f/name).write_text(original)
        (self.f/"library").unlink()
        self.assertEqual(decision("resume.sh", self.f), "NATIVE")
    def test_missing_tools(self):
        env = dict(os.environ, PATH=str(self.f))
        self.assertEqual(decision("selector.sh", self.f, env), "NATIVE")
    def test_capture(self):
        spec=importlib.util.spec_from_file_location("workflow", ROOT/"metol/workflow.py")
        import sys
        module=importlib.util.module_from_spec(spec); sys.modules[spec.name]=module; spec.loader.exec_module(module)
        a=module.Capture().run(False, True)
        self.assertEqual(a.events, ["preparation-check", "ready"]); self.assertTrue(a.ready)
        b=module.Capture().run(True, False)
        self.assertFalse(b.zram_owned); self.assertFalse(b.led_owned); self.assertTrue(b.ready)
        c=module.Capture().run(True, True)
        self.assertTrue(c.header_allowed); self.assertLess(c.events.index("header-check"), c.events.index("led-acquired"))
    def test_archive(self):
        spec = importlib.util.spec_from_file_location("archive_test", ROOT / "build_payload.py")
        module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
        for name in module.FILES:
            if name == 'out/eye28-verify.arm': continue
            dest = self.f / name; dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / name, dest)
        archive = self.f / 'out/eye28-metol-offline-1.zip'
        collect = module.collect
        with mock.patch.object(module, 'ROOT', self.f), mock.patch.object(module, 'collect', side_effect=lambda: collect(self.f)):
            with mock.patch.object(module, 'REVIEWED_HELPER_SHA256', None):
                with self.assertRaisesRegex(ValueError, 'helper has not been reviewed'): module.main()
                self.assertFalse(archive.exists())
            helper = self.f / 'out/eye28-verify.arm'; helper.write_bytes(b'fixture helper')
            with mock.patch.object(module, 'REVIEWED_HELPER_SHA256', hashlib.sha256(helper.read_bytes()).hexdigest()):
                module.main()
        with zipfile.ZipFile(archive) as z:
            manifest = json.loads(z.read('manifest.json'))
            self.assertFalse(manifest['deployment_allowed'])
            self.assertEqual(set(z.namelist()), set(module.FILES) | {'manifest.json'})
            self.assertEqual(set(manifest['files']), set(module.FILES))
            for name, digest in manifest['files'].items():
                self.assertEqual(hashlib.sha256(z.read(name)).hexdigest(), digest)

if __name__ == "__main__": unittest.main()
