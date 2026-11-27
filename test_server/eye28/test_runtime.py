import hashlib, json, os, subprocess, tempfile, unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]/'contrib/nx500-eye28'
class Runtime(unittest.TestCase):
 def setUp(self):
  self.t=tempfile.TemporaryDirectory(); self.addCleanup(self.t.cleanup); self.r=Path(self.t.name)
  def put(path, data):
   p=self.r/path.lstrip('/'); p.parent.mkdir(parents=True,exist_ok=True); p.write_text(data); return p
  self.put=put
  app=put('/opt/App','fake app'); lib=self.r/'opt/eye.so'; lib.write_bytes((ROOT/'out/nxl-eye-af.so').read_bytes())
  sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
  self.profile={'schema':1,'token':'/tmp/nx-lut-lite/eye-af.resume-ready','app':{'path':'/opt/App','sha256':sha(app)},'libraries':[{'path':'/opt/eye.so','sha256':sha(lib)}],'swap':'/dev/zram0','build_id':'v1','report_id':'r1'}
  self.proof={'pid':123,'starttime':'42','boot_id':'boot','generation':'g1','image_generation':'g1','build_id':'v1','report_id':'r1'}
  put('/proc/sys/kernel/random/boot_id','boot\n'); put('/proc/swaps','Filename Type Size Used Priority\n/dev/zram0 partition 1 0 -1\n')
  put('/proc/123/stat','123 (fake App) '+ ' '.join(['S']+['0']*18+['42'])+'\n')
  (self.r/'proc/123/exe').symlink_to('/opt/App')
  st=lib.stat(); put('/proc/123/maps',f'1000-2000 r-xp 00000000 {os.major(st.st_dev):x}:{os.minor(st.st_dev):x} {st.st_ino} /opt/eye.so\n')
  self.p=self.r/'profile.json'; self.q=self.r/'proof.json'; self.save()
 def save(self): self.p.write_text(json.dumps(self.profile)); self.q.write_text(json.dumps(self.proof))
 def runcheck(self):
  self.save(); return subprocess.check_output(['python3',str(ROOT/'runtime/verify.py'),'launch','--test-root',str(self.r),'--profile',str(self.p),'--proof',str(self.q),'--pid','123'],text=True).strip()
 def test_valid_and_identity_failures(self):
  self.assertEqual(self.runcheck(),'CANDIDATE')
  for key in ('pid','starttime','boot_id','generation','image_generation','build_id','report_id'):
   old=self.proof[key]; self.proof[key]='bad'; self.assertEqual(self.runcheck(),'NATIVE',key); self.proof[key]=old
  self.put('/proc/123/maps','1000-2000 r-xp 00000000 00:00 1 /opt/eye.so\n'); self.assertEqual(self.runcheck(),'NATIVE')
 def test_token_symlink_and_hash(self):
  self.put('/tmp/nx-lut-lite/eye-af.resume-ready','old'); self.assertEqual(self.runcheck(),'NATIVE')
  (self.r/'tmp/nx-lut-lite/eye-af.resume-ready').unlink()
  (self.r/'opt/eye.so').write_bytes(b'changed'); self.assertEqual(self.runcheck(),'NATIVE')
  (self.r/'opt/eye.so').unlink(); (self.r/'opt/eye.so').symlink_to('/etc/passwd'); self.assertEqual(self.runcheck(),'NATIVE')
 def test_production_disabled(self):
  self.assertEqual(subprocess.check_output(['/bin/sh',str(ROOT/'runtime/launch.sh')],text=True).strip(),'NATIVE')
if __name__=='__main__': unittest.main()
