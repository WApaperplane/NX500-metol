import hashlib, importlib.util, json, os, subprocess, sys, tempfile, unittest
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('installer', ROOT / 'tools/extensions/installer.py')
i = importlib.util.module_from_spec(spec); spec.loader.exec_module(i)

class InstallerTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(); self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name); self.payload = self.root/'payload'; self.payload.mkdir()
        self.sd = self.root/'sd'; self.sd.mkdir(); (self.payload/'x').write_bytes(b'payload')
        self.m = {'schema_version':1,'model':'NX500','firmware':'1.12','profile':'log117','upstream':{'commit':'abc'},'target_root':'/opt/usr/nx-ks/gamma','files':[{'path':'x','sha256':hashlib.sha256(b'payload').hexdigest(),'mode':'0644'}]}
        self.manifest = self.root/'manifest.json'; self.save()
    def save(self): self.manifest.write_text(json.dumps(self.m))
    def stage(self, volume='V'): return i.stage(self.manifest,self.payload,self.sd,volume,lambda _: 'V')
    def test_escape(self):
        for path in ('../x','/x','a/../../x','C:/x','a\\x'):
            self.m['files'][0]['path']=path; self.save()
            with self.assertRaises(ValueError): i.inspect(self.manifest,self.payload)
    def test_symlink(self):
        (self.payload/'link').symlink_to(self.payload/'x'); self.m['files'][0]['path']='link'; self.save()
        with self.assertRaises(ValueError): self.stage()
    def test_bad_hash_and_target(self):
        self.m['files'][0]['sha256']='0'*64; self.save()
        with self.assertRaises(ValueError): self.stage()
        self.m['files'][0]['sha256']=hashlib.sha256(b'payload').hexdigest(); self.m['target_root']='/opt/usr/nx-ks/other'; self.save()
        with self.assertRaises(ValueError): self.stage()
    def test_volume_duplicate_and_status(self):
        with self.assertRaises(ValueError): self.stage('WRONG')
        result=self.stage()
        with self.assertRaises(ValueError): self.stage()
        self.assertFalse(result['camera_install_requested']); self.assertFalse((self.sd/'info.tg').exists())
        self.assertEqual(i.status(self.sd,result['request_id']),result)
        status=self.sd/'.nxwa-extensions'/result['request_id']/'status.json'; data=json.loads(status.read_text()); data['request_id']='0'*32; status.write_text(json.dumps(data))
        with self.assertRaises(ValueError): i.status(self.sd,result['request_id'])
    def test_trigger(self):
        (self.sd/'info.tg').write_text('existing')
        with self.assertRaises(ValueError): self.stage()
    def test_cli(self):
        p=subprocess.run([sys.executable,str(ROOT/'tools/extensions/installer.py'),'check','--manifest',str(self.manifest),'--payload',str(self.payload)],capture_output=True,text=True)
        self.assertEqual(p.returncode,0,p.stderr); self.assertEqual(json.loads(p.stdout)['state'],'CHECKED')

if __name__ == '__main__': unittest.main()
