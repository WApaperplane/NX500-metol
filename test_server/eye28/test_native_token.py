import ctypes, multiprocessing, os, subprocess, tempfile, unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]/'contrib/nx500-eye28/runtime'
CALL=ctypes.CFUNCTYPE(ctypes.c_int,ctypes.c_void_p)
def invoke(libpath,root,pid='123',start='42',gen='g1',fail_at=0):
 lib=ctypes.CDLL(str(libpath)); fun=lib.eye_publish_token
 fun.argtypes=[ctypes.c_char_p]*4+[CALL,ctypes.c_void_p]; fun.restype=ctypes.c_int
 count=[0]
 @CALL
 def cb(_):
  count[0]+=1
  return count[0]!=fail_at
 return fun(str(root).encode(),pid.encode(),start.encode(),gen.encode(),cb,None)
def worker(lib,root,queue): queue.put(invoke(lib,root))
class Token(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.work=tempfile.TemporaryDirectory(); cls.lib=Path(cls.work.name)/'token.so'; cls.prod=Path(cls.work.name)/'prod.so'
  for path,flags in ((cls.lib,['-DTEST_BUILD']),(cls.prod,[])):
   subprocess.run(['cc','-shared','-fPIC','-Wall','-Wextra','-Werror',*flags,'-o',str(path),str(ROOT/'token.c')],check=True)
 @classmethod
 def tearDownClass(cls): cls.work.cleanup()
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory(); self.addCleanup(self.temp.cleanup); self.root=Path(self.temp.name); self.d=self.root/'tmp/nx-lut-lite'; self.d.mkdir(parents=True)
 def test_repeat_and_old(self):
  self.assertEqual(invoke(self.lib,self.root),1); self.assertEqual((self.d/'eye-af.resume-ready').read_text(),'123\n')
  self.assertEqual(invoke(self.lib,self.root),1); self.assertEqual(invoke(self.lib,self.root,start='43'),0)
 def test_token_embedded_nul(self):
  self.assertEqual(invoke(self.lib,self.root),1)
  (self.d/'eye-af.resume-ready').write_bytes(b'123\n\x00JUNK')
  self.assertEqual(invoke(self.lib,self.root),0)
 def test_trailing_data_and_sidecar(self):
  self.assertEqual(invoke(self.lib,self.root),1)
  token=self.d/'eye-af.resume-ready'; side=self.d/'eye-af.resume-ready.identity'
  token.write_bytes(b'123\nextra'); self.assertEqual(invoke(self.lib,self.root),0)
  token.write_bytes(b'123\n'); original=side.read_bytes()
  side.write_bytes(original+b'\x00junk'); self.assertEqual(invoke(self.lib,self.root),0)
  side.unlink(); side.symlink_to('/dev/null'); self.assertEqual(invoke(self.lib,self.root),0)
  side.unlink(); os.mkfifo(side); self.assertEqual(invoke(self.lib,self.root),0)
 def test_unsafe_directory(self):
  self.d.chmod(0o777); self.assertEqual(invoke(self.lib,self.root),0); self.assertEqual(list(self.d.iterdir()),[])
 def test_old_token(self):
  (self.d/'eye-af.resume-ready').write_text('123\n'); self.assertEqual(invoke(self.lib,self.root),0)
 def test_symlinks(self):
  (self.d/'eye-af.resume-ready').symlink_to('/dev/null'); self.assertEqual(invoke(self.lib,self.root),0)
  (self.d/'eye-af.resume-ready').unlink(); self.d.rmdir(); self.d.symlink_to('/tmp'); self.assertEqual(invoke(self.lib,self.root),0)
 def test_callback_cleanup(self):
  for n in (1,2,3):
   self.assertEqual(invoke(self.lib,self.root,fail_at=n),0); self.assertEqual(list(self.d.iterdir()),[])
 def test_io_failure(self):
  (self.d/'.eye28.token.tmp').mkdir(); self.assertEqual(invoke(self.lib,self.root),0); self.assertEqual(sorted(x.name for x in self.d.iterdir()),['.eye28.token.tmp'])
 def test_two_processes(self):
  q=multiprocessing.Queue(); ps=[multiprocessing.Process(target=worker,args=(self.lib,self.root,q)) for _ in range(2)]
  for p in ps:p.start()
  for p in ps:p.join(); self.assertEqual(p.exitcode,0)
  results=[q.get(timeout=2) for _ in ps]; self.assertIn(1,results); self.assertEqual(invoke(self.lib,self.root),1)
 def test_production_disabled(self): self.assertEqual(invoke(self.prod,self.root),0); self.assertEqual(list(self.d.iterdir()),[])
if __name__=='__main__': unittest.main()
