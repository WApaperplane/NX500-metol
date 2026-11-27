import pathlib, shutil, subprocess, tempfile, unittest
import test_server.eye28.test_native_integration as integration

ROOT = pathlib.Path(__file__).resolve().parents[2] / 'contrib/nx500-eye28'

class Wrappers(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory(); self.addCleanup(self.tmp.cleanup)
  self.root=pathlib.Path(self.tmp.name)
  for folder in ('runtime','metol','out'): (self.root/folder).mkdir()
  for folder, names in (('runtime',('launch.sh','resume.sh')),('metol',('selector.sh','resume.sh'))):
   for name in names: shutil.copy(ROOT/folder/name,self.root/folder/name)
  self.helper=self.root/'out/eye28-verify.arm'
 def call(self,folder,name):
  p=subprocess.run(['sh',str(self.root/folder/name)],capture_output=True,text=True)
  self.assertEqual(p.returncode,0); return p.stdout
 def stub(self, body):
  self.helper.write_text('#!/bin/sh\n'+body+'\n'); self.helper.chmod(0o755)
 def test_missing(self):
  for name in ('launch.sh','resume.sh'): self.assertEqual(self.call('runtime',name),'NATIVE\n')
 def test_protocol(self):
  for body in ("echo CANDIDATE; exit 1", "printf 'CANDIDATE\\nextra\\n'", "printf 'CANDIDATE\\n\\n'", 'echo ENABLE'):
   self.stub(body); self.assertEqual(self.call('metol','selector.sh'),'NATIVE\n')
  self.stub('echo CANDIDATE'); self.assertEqual(self.call('metol','selector.sh'),'CANDIDATE\n')
  self.stub('echo ENABLE'); self.assertEqual(self.call('metol','resume.sh'),'ENABLE\n')
  self.stub('echo CANDIDATE'); self.assertEqual(self.call('metol','resume.sh'),'NATIVE\n')
 def test_production_helper(self):
  subprocess.run(['cc','-Wall','-Wextra','-Werror','-O2',*[str(p) for p in sorted((ROOT/'runtime').glob('*.c'))],'-o',str(self.helper)],check=True)
  for name in ('launch.sh','resume.sh'): self.assertEqual(self.call('runtime',name),'NATIVE\n')

class WrapperIntegration(integration.Integration):
 def setUp(self):
  super().setUp()
  bundle=self.root/'bundle'
  for folder in ('runtime','metol','out'): (bundle/folder).mkdir(parents=True)
  for folder,names in (('runtime',('launch.sh','resume.sh')),('metol',('selector.sh','resume.sh'))):
   for name in names: shutil.copy(ROOT/folder/name,bundle/folder/name)
  self.bundle=bundle; self.helper=bundle/'out/eye28-verify.arm'; shutil.copy(self.exe,self.helper)
 def runverify(self,op):
  name='selector.sh' if op=='launch' else 'resume.sh'
  return subprocess.check_output(['sh',str(self.bundle/'metol'/name),str(self.root),'123'],text=True).strip()
 def test_wrapper_end_to_end(self):
  def call(name): return subprocess.check_output(['sh',str(self.bundle/'metol'/name),str(self.root),'123'],text=True)
  self.put('/proc/swaps','Filename Type Size Used Priority\n/dev/mmcblk0p9 partition 1 0 -1\n')
  stat=self.root/'proc/123/stat'; saved=stat.read_text(); stat.unlink()
  self.assertEqual(call('selector.sh'),'CANDIDATE\n')
  stat.write_text(saved); self.put('/proc/swaps','Filename Type Size Used Priority\n')
  self.assertEqual(call('resume.sh'),'ENABLE\n')
  self.assertTrue((self.root/'tmp/nx-lut-lite/eye-af.resume-ready').exists())
  self.put('/proof','bad'); self.assertEqual(call('resume.sh'),'NATIVE\n')
  shutil.copy(self.prod,self.helper); self.assertEqual(call('selector.sh'),'NATIVE\n')

if __name__=='__main__': unittest.main()
