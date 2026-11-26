import json, pathlib, subprocess, tempfile, unittest
ROOT=pathlib.Path(__file__).resolve().parents[2]/'contrib/nx500-eye-af'
class Bundle(unittest.TestCase):
 def test_integrity(self):
  with tempfile.TemporaryDirectory() as temp:
   bundle=pathlib.Path(temp)/'bundle'
   subprocess.run(['python3',str(ROOT/'build_metol_bundle.py'),'--output',str(bundle)],check=True,stdout=subprocess.DEVNULL)
   def verify(): return subprocess.run(['sh',str(bundle/'verify-payload.sh'),str(bundle)],capture_output=True).returncode
   self.assertEqual(verify(),0)
   for name in json.loads((bundle/'manifest.json').read_text())['sha256']:
    p=bundle/name; original=p.read_bytes(); p.write_bytes(original+b'changed')
    self.assertNotEqual(verify(),0,name); p.write_bytes(original)
   self.assertEqual(verify(),0)
if __name__=='__main__': unittest.main()
