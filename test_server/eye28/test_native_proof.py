import ctypes, os, subprocess, tempfile, unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]/'contrib/nx500-eye28/runtime'
class Proof(ctypes.Structure):
 _fields_=[(n,ctypes.c_char*129) for n in ('generation','image_generation','boot_id','pid','starttime','build_id','report_id')]
class Parser(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.tmp=tempfile.TemporaryDirectory(); cls.libpath=Path(cls.tmp.name)/'proof.so'
  subprocess.run(['cc','-shared','-fPIC','-Wall','-Wextra','-Werror','-o',str(cls.libpath),str(ROOT/'proof.c')],check=True)
  cls.lib=ctypes.CDLL(str(cls.libpath)); cls.lib.eye_read_proof.argtypes=[ctypes.c_char_p,ctypes.POINTER(Proof)]; cls.lib.eye_read_proof.restype=ctypes.c_int
 @classmethod
 def tearDownClass(cls): cls.tmp.cleanup()
 def setUp(self):
  self.file=Path(self.tmp.name)/'proof'; self.file.unlink(missing_ok=True)
  self.data=b'generation=g1\nimage_generation=g2\nboot_id=b1\npid=123\nstarttime=42\nbuild_id=v1\nreport_id=r1\n'
 def check(self,data):
  self.file.write_bytes(data); result=Proof(); return self.lib.eye_read_proof(str(self.file).encode(),ctypes.byref(result))
 def test_valid(self): self.assertEqual(self.check(self.data),1)
 def test_duplicate(self): self.assertEqual(self.check(self.data+b'pid=2\n'),0)
 def test_missing(self): self.assertEqual(self.check(self.data.replace(b'pid=123\n',b'')),0)
 def test_unknown(self): self.assertEqual(self.check(self.data+b'unknown=x\n'),0)
 def test_long(self): self.assertEqual(self.check(self.data.replace(b'generation=g1',b'generation='+b'a'*129)),0)
 def test_numbers(self):
  for v in (b'0',b'-1',b'12x',b''):
   self.assertEqual(self.check(self.data.replace(b'pid=123',b'pid='+v)),0)
 def test_control(self): self.assertEqual(self.check(self.data.replace(b'boot_id=b1',b'boot_id=b\x011')),0)
 def test_nul(self): self.assertEqual(self.check(self.data.replace(b'boot_id=b1',b'boot_id=b\x001')),0)
 def test_overflow(self): self.assertEqual(self.check(self.data.replace(b'pid=123',b'pid='+b'9'*128)),0)
 def test_fifo(self):
  os.mkfifo(self.file)
  self.assertEqual(self.lib.eye_read_proof(str(self.file).encode(),ctypes.byref(Proof())),0)
 def test_ancestor_symlink(self):
  self.file.write_bytes(self.data)
  link=Path(self.tmp.name)/'link'; link.symlink_to(Path(self.tmp.name),target_is_directory=True)
  try: self.assertEqual(self.lib.eye_read_proof(str(link/'proof').encode(),ctypes.byref(Proof())),0)
  finally: link.unlink()
 def test_symlink(self):
  target=Path(self.tmp.name)/'target'; target.write_bytes(self.data); self.file.symlink_to(target)
  self.assertEqual(self.lib.eye_read_proof(str(self.file).encode(),ctypes.byref(Proof())),0)
if __name__=='__main__': unittest.main()
