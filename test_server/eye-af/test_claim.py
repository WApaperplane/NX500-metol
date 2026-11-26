import pathlib, subprocess, tempfile, unittest
ROOT = pathlib.Path(__file__).resolve().parents[2] / 'contrib/nx500-eye-af/metol'
class Claim(unittest.TestCase):
 def test_exclusive_and_mapping(self):
  with tempfile.TemporaryDirectory() as tmp:
   tmp = pathlib.Path(tmp); binary = tmp/'claim'; library = tmp/'lib.so'; library.write_bytes(b'test')
   subprocess.run(['gcc','-Wall','-Wextra','-Werror',str(ROOT/'claim.c'),'-o',str(binary)],check=True)
   args=[str(binary),str(tmp),'new','3','123','EYE',str(library)]
   self.assertEqual(subprocess.run(args).returncode,0)
   claim=(tmp/'launch-claim').read_text(); self.assertIn('camera_pid=123\n',claim); self.assertIn('mapped_ino='+str(library.stat().st_ino)+'\n',claim)
   self.assertNotEqual(subprocess.run(args).returncode,0)
if __name__ == '__main__': unittest.main()
