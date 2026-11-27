import hashlib, importlib.util, shutil, tempfile, unittest
from unittest import mock
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]/'contrib/nx500-eye28'
spec=importlib.util.spec_from_file_location('packaging_under_test', ROOT/'build_payload.py')
module=importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
class Packaging(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory(); self.addCleanup(self.temp.cleanup); self.root=Path(self.temp.name)
  for name in module.FILES:
   if name=='out/eye28-verify.arm': continue
   dest=self.root/name; dest.parent.mkdir(parents=True, exist_ok=True); shutil.copyfile(ROOT/name,dest)
 def test_modified_library_rejected(self):
  (self.root/'out/nxl-eye-af.so').write_bytes(b'tampered')
  with self.assertRaisesRegex(ValueError,'library hash'): module.collect(self.root)
 def test_source_symlink_rejected(self):
  path=self.root/'src/eye-af.c'; path.unlink(); path.symlink_to(ROOT/'src/eye-af.c')
  with self.assertRaisesRegex(ValueError,'symlink'): module.collect(self.root)
 def test_path_traversal_rejected(self):
  for path in ('../LICENSE','/etc/passwd','src/../../LICENSE'):
   with self.assertRaises(ValueError): module.read_safe(self.root,path)
 def test_license_and_helper_gate(self):
  with mock.patch.object(module, 'REVIEWED_HELPER_SHA256', None):
   with self.assertRaisesRegex(ValueError,'helper has not been reviewed'): module.collect(self.root)
  (self.root/'LICENSE').write_text('changed')
  with self.assertRaisesRegex(ValueError,'license hash'): module.collect(self.root)
 def test_allowlist_and_pinned_helper(self):
  h=self.root/'out/eye28-verify.arm'; h.write_bytes(b'fixture helper')
  old=module.REVIEWED_HELPER_SHA256; module.REVIEWED_HELPER_SHA256=hashlib.sha256(h.read_bytes()).hexdigest()
  self.addCleanup(setattr,module,'REVIEWED_HELPER_SHA256',old)
  (self.root/'out/host-helper').write_bytes(b'ignored')
  files=module.collect(self.root)
  self.assertEqual(set(files),set(module.FILES)); self.assertNotIn('out/host-helper',files)
  self.assertNotIn('runtime/verify.py',files)
  self.assertFalse(any(n.endswith(('.o','.exe')) for n in files))
  h.write_bytes(b'changed')
  with self.assertRaisesRegex(ValueError,'helper hash mismatch'): module.collect(self.root)
if __name__=='__main__': unittest.main()
