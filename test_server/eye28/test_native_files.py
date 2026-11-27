import hashlib, os, pathlib, subprocess, tempfile, unittest
SRC=pathlib.Path(__file__).resolve().parents[2]/'contrib/nx500-eye28/runtime'
class Files(unittest.TestCase):
 def setUp(self):
  self.t=tempfile.TemporaryDirectory(); self.r=pathlib.Path(self.t.name); (self.r/'lib').mkdir(); self.f=self.r/'lib/a'; self.f.write_bytes(b'fixture'); self.h=hashlib.sha256(b'fixture').hexdigest()
  (self.r/'proc/1').mkdir(parents=True)
  c=self.r/'m.c'; c.write_text('#include "file_identity.h"\n#include <string.h>\nint main(int c,char**v){struct stat s;if(c!=5)return 2;if(eye_verify_file(v[1],v[2],v[3],&s))return 1;return strcmp(v[4],"file")?eye_mapping_matches(v[1],1,v[4],&s)!=0:0;}\n')
  self.exe=self.r/'run'; subprocess.run(['cc','-DTEST_BUILD','-std=c99','-Wall','-Wextra','-Werror','-I',str(SRC),str(c),str(SRC/'file_identity.c'),'-o',str(self.exe)],check=True)
 def tearDown(self): self.t.cleanup()
 def runcheck(self,path='/lib/a',digest=None,mapping='file'): return subprocess.run([str(self.exe),str(self.r),path,digest or self.h,mapping]).returncode
 def maps(self,perms='r-xp',ino=None,dev=None,path='/lib/a'):
  s=self.f.stat(); d=dev or (os.major(s.st_dev),os.minor(s.st_dev)); (self.r/'proc/1/maps').write_text('1000-2000 %s 00000000 %x:%x %d %s\n'%(perms,*d,ino if ino is not None else s.st_ino,path))
 def test_valid(self): self.assertEqual(self.runcheck(),0)
 def test_bad_hash(self): self.assertNotEqual(self.runcheck(digest='0'*64),0)
 def test_leaf_symlink(self): (self.r/'lib/b').symlink_to('a'); self.assertNotEqual(self.runcheck('/lib/b'),0)
 def test_parent_symlink(self): (self.r/'link').symlink_to('lib',target_is_directory=True); self.assertNotEqual(self.runcheck('/link/a'),0)
 def test_mapping(self): self.maps(); self.assertEqual(self.runcheck(mapping='/lib/a'),0)
 def test_non_executable(self): self.maps(perms='r--p'); self.assertNotEqual(self.runcheck(mapping='/lib/a'),0)
 def test_wrong_inode(self): self.maps(ino=1); self.assertNotEqual(self.runcheck(mapping='/lib/a'),0)
 def test_wrong_device(self): self.maps(dev=(0,0)); self.assertNotEqual(self.runcheck(mapping='/lib/a'),0)
 def test_wrong_path(self): self.maps(path='/other'); self.assertNotEqual(self.runcheck(mapping='/lib/a'),0)
 def test_root_ancestor_symlink(self):
  link=self.r.parent/(self.r.name+'-link'); link.symlink_to(self.r, target_is_directory=True)
  try: self.assertNotEqual(subprocess.run([str(self.exe),str(link),'/lib/a',self.h,'file']).returncode,0)
  finally: link.unlink()
 def test_maps_symlink(self):
  (self.r/'proc/1/maps').symlink_to(self.f)
  self.assertNotEqual(self.runcheck(mapping='/lib/a'),0)
 def test_fifo(self):
  os.mkfifo(self.r/'lib/fifo')
  result=subprocess.run([str(self.exe),str(self.r),'/lib/fifo',self.h,'file'],timeout=2)
  self.assertNotEqual(result.returncode,0)
if __name__=='__main__': unittest.main()
