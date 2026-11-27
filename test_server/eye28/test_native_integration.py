import hashlib, os, subprocess, tempfile, unittest
from pathlib import Path
R=Path(__file__).resolve().parents[2]/'contrib/nx500-eye28/runtime'
class Integration(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.work=tempfile.TemporaryDirectory(); cls.exe=Path(cls.work.name)/'verify'; cls.prod=Path(cls.work.name)/'prod'; sha=hashlib.sha256(b'fixture').hexdigest()
  files=[str(R/(x+'.c')) for x in ('verify','proc_identity','file_identity','proof','token')]
  subprocess.run(['cc','-DTEST_BUILD','-DEYE_TEST_FAULT_INJECTION','-DFIXTURE_SHA="'+sha+'"','-Wall','-Wextra','-Werror',*files,'-o',str(cls.exe)],check=True)
  subprocess.run(['cc','-Wall','-Wextra','-Werror',str(R/'verify.c'),'-o',str(cls.prod)],check=True)
 @classmethod
 def tearDownClass(cls): cls.work.cleanup()
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory(); self.addCleanup(self.temp.cleanup); self.root=Path(self.temp.name)
  libs=[self.put('/opt/'+name,'fixture') for name in ('App','eye.so','libface.so','libcapture.so')]
  self.put('/proc/123/stat','123 (fake App) '+' '.join(['S']+['0']*18+['42'])+'\n')
  (self.root/'proc/123/exe').symlink_to('/opt/App')
  self.put('/proc/sys/kernel/random/boot_id','boot\n')
  self.put('/proc/123/maps',''.join(f'1000-2000 r-xp 00000000 {os.major(p.stat().st_dev):x}:{os.minor(p.stat().st_dev):x} {p.stat().st_ino} /opt/{p.name}\n' for p in libs))
  self.put('/run/eye28.bootmode','capture\n')
  self.put('/proc/swaps','Filename Type Size Used Priority\n')
  self.put('/tmp/.hib_post','ready'); (self.root/'tmp/nx-lut-lite').mkdir()
  self.put('/proof','generation=g1\nimage_generation=g1\nboot_id=boot\npid=123\nstarttime=42\nbuild_id=fixture-build\nreport_id=fixture-report\n')
 def put(self,p,s):
  x=self.root/p.lstrip('/'); x.parent.mkdir(parents=True,exist_ok=True); x.write_text(s); return x
 def runverify(self,op): return subprocess.check_output([str(self.exe),op,str(self.root),'123'],text=True).strip()
 def test_resume_and_repeat(self):
  self.assertEqual(self.runverify('resume'),'ENABLE'); self.assertEqual(self.runverify('resume'),'ENABLE')
 def test_launch(self):
  self.put('/proc/swaps','Filename Type Size Used Priority\n/dev/mmcblk0p9 partition 1 0 -1\n'); (self.root/'proc/123/stat').unlink(); self.assertEqual(self.runverify('launch'),'CANDIDATE')
 def test_denials(self):
  for path,data in [('/proof',''),('/proc/123/maps',''),('/proc/swaps','Filename Type Size Used Priority\n/dev/mmcblk0p9 partition 1 0 -1\n'),('/opt/App','bad'),('/run/eye28.bootmode','bad')]:
   p=self.root/path.lstrip('/'); old=p.read_text(); p.write_text(data); self.assertEqual(self.runverify('resume'),'NATIVE',path); p.write_text(old)
  p=self.root/'proof'; old=p.read_text(); p.write_text(old.replace('image_generation=g1','image_generation=old')); self.assertEqual(self.runverify('resume'),'NATIVE')
 def test_swap_tab_and_malformed(self):
  self.put('/proc/swaps','Filename Type Size Used Priority\n/dev/mmcblk0p9\tpartition\t1\t0\t-1\n')
  self.assertEqual(self.runverify('resume'),'NATIVE')
  for value in ('bad header\n','Filename Type Size Used Priority\n/dev/x partition X 0 -1\n','Filename Type Size Used Priority\n/dev/x partition 1 0 -1 extra\n'):
   self.put('/proc/swaps',value); self.assertEqual(self.runverify('resume'),'NATIVE')
 def test_links_and_io(self):
  for name in ('proc/swaps','tmp/.hib_post'):
   p=self.root/name; old=p.read_text(); p.unlink(); p.symlink_to('/dev/null'); self.assertEqual(self.runverify('resume'),'NATIVE'); p.unlink(); p.write_text(old)
  import shutil
  shutil.rmtree(self.root/'tmp/nx-lut-lite'); self.assertEqual(self.runverify('resume'),'NATIVE')
 def test_pid_reuse_and_invalid(self):
  p=self.root/'proc/123/stat'; p.write_text(p.read_text().replace(' 42\n',' 43\n')); self.assertEqual(self.runverify('resume'),'NATIVE')
  self.assertEqual(subprocess.check_output([str(self.exe),'unknown',str(self.root),'123'],text=True).strip(),'NATIVE')
  self.assertEqual(subprocess.check_output([str(self.exe),'resume','/','123'],text=True).strip(),'NATIVE')
  self.assertEqual(subprocess.check_output([str(self.exe),'resume',str(self.root),'9'*200],text=True).strip(),'NATIVE')
 def test_evidence_changes_before_callback(self):
  for name,path in [('proof','proof'),('maps','proc/123/maps'),('library','opt/eye.so')]:
   p=self.root/path; original=p.read_bytes(); env=dict(os.environ,EYE_TEST_REMOVE=name)
   result=subprocess.check_output([str(self.exe),'resume',str(self.root),'123'],text=True,env=env).strip()
   self.assertEqual(result,'NATIVE',name); self.assertFalse((self.root/'tmp/nx-lut-lite/eye-af.resume-ready').exists()); p.write_bytes(original)
   if name=='maps': pass
   if name=='library':
    st=p.stat(); maps=self.root/'proc/123/maps'; lines=maps.read_text().splitlines(); lines=[f'1000-2000 r-xp 00000000 {os.major(st.st_dev):x}:{os.minor(st.st_dev):x} {st.st_ino} /opt/eye.so' if x.endswith('/opt/eye.so') else x for x in lines]; maps.write_text('\n'.join(lines)+'\n')
 def test_production(self): self.assertEqual(subprocess.check_output([str(self.prod),'resume',str(self.root),'123'],text=True).strip(),'NATIVE')
if __name__=='__main__': unittest.main()
