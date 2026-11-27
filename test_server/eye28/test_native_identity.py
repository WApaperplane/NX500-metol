import pathlib, subprocess, tempfile, unittest

SRC = pathlib.Path(__file__).resolve().parents[2] / 'contrib/nx500-eye28/runtime'

class IdentityTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.tmp.name)
        harness = self.root / 'main.c'
        harness.write_text('#include "proc_identity.h"\n#include <stdio.h>\n#include <stdlib.h>\nint main(int c,char**v){struct eye_identity i;if(c!=3||eye_read_identity(v[1],atol(v[2]),&i))return 1;printf("%llu %s %s\\n",i.starttime,i.exe,i.boot_id);return 0;}\n')
        self.bin = self.root / 'test'
        subprocess.run(['cc','-DTEST_BUILD','-std=c99','-Wall','-Wextra','-Werror','-I',str(SRC),str(harness),str(SRC/'proc_identity.c'),'-o',str(self.bin)],check=True)
        self.proc = self.root/'proc/123'
        self.proc.mkdir(parents=True)
        self.boot = self.root/'proc/sys/kernel/random/boot_id'
        self.boot.parent.mkdir(parents=True)
        self.boot.write_text('abc-123\n')
        (self.proc/'exe').symlink_to('/app')
        self.stat('123 (hello world) S ' + ' '.join(['0']*18) + ' 987 0\n')
    def tearDown(self): self.tmp.cleanup()
    def stat(self, value): (self.proc/'stat').write_text(value)
    def run_helper(self, pid='123'): return subprocess.run([str(self.bin),str(self.root),pid],capture_output=True,text=True)
    def test_valid(self): self.assertEqual(self.run_helper().stdout,'987 /app abc-123\n')
    def test_bad_stat_and_pid(self):
        self.stat('124 (x) S 0\n')
        self.assertNotEqual(self.run_helper().returncode,0)
        self.assertNotEqual(self.run_helper('0').returncode,0)
    def test_bad_boot(self):
        self.boot.write_text('bad boot\n')
        self.assertNotEqual(self.run_helper().returncode,0)
    def test_oversized_stat(self):
        self.stat('123 (' + 'a'*9000 + ') S ' + '0 '*20)
        self.assertNotEqual(self.run_helper().returncode,0)
    def test_symlinks_and_root(self):
        for p in (self.proc/'stat', self.boot):
            original=p.read_bytes(); p.unlink(); p.symlink_to('/etc/passwd')
            self.assertNotEqual(self.run_helper().returncode,0)
            p.unlink(); p.write_bytes(original)
        link=self.root.parent/(self.root.name+'-link'); link.symlink_to(self.root, target_is_directory=True)
        try:
            for root in (link, pathlib.Path('/')):
                self.assertNotEqual(subprocess.run([str(self.bin),str(root),'123']).returncode,0)
        finally: link.unlink()

if __name__ == '__main__': unittest.main()
