import pathlib, subprocess, tempfile, unittest

WAKE = pathlib.Path(__file__).resolve().parents[2] / 'contrib/nx500-eye-af/metol/wake.sh'
LIB = '/usr/local/lib/nx-eye-af/nxl-eye-af-yunet.so'

class Resume(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(); self.root = pathlib.Path(self.tmp.name)
        self.put('sys/class/extcon/USB/state', '0\n'); self.put('sys/power/bootmode', '2\n')
        self.put('tmp/.hib_post', ''); self.put('proc/swaps', 'Filename Type Size Used Priority\n')
        self.put('proc/sys/kernel/random/boot_id', 'new\n'); self.put('test-dmesg', 'kernel_resume start!\n')
        self.put('opt/usr/nx-eye-af/armed', 'old\nEYE\n'); self.put('opt/usr/nx-eye-af/capture-origin', 'old\nEYE\n')
        self.put('opt/usr/nx-eye-af/launch-claim', f'boot_id=new\npolicy=EYE\ncamera_pid=123\nfile={LIB}\nmapped_dev=01:02\nmapped_ino=42\n')
        self.put('proc/123/maps', f'100-200 r-xp 0 01:02 42 {LIB}\n')
        (self.root / 'proc/123/exe').symlink_to('/usr/apps/com.samsung.di-camera-app/bin/di-camera-app')
    def tearDown(self): self.tmp.cleanup()
    def put(self, name, text):
        p = self.root / name; p.parent.mkdir(parents=True, exist_ok=True); p.write_text(text)
    def run_wake(self): return subprocess.run(['sh', str(WAKE), '--test-root', str(self.root)], capture_output=True, text=True)
    def test_success_and_idempotence(self):
        self.assertEqual(self.run_wake().returncode, 0); self.assertEqual(self.run_wake().returncode, 0)
        self.assertEqual((self.root / 'tmp/nx-lut-lite/eye-af.resume-ready').read_text(), '123\n')
    def test_rejections(self):
        for path, value in [('sys/class/extcon/USB/state','1'), ('proc/123/maps',''), ('opt/usr/nx-eye-af/capture-origin','other\nEYE\n'), ('test-dmesg','')]:
            p=self.root/path; old=p.read_text(); p.write_text(value)
            self.assertNotEqual(self.run_wake().returncode, 0, path); p.write_text(old)
        self.assertFalse((self.root / 'tmp/nx-lut-lite/eye-af.resume-ready').exists())
    def test_conflicting_token(self):
        self.put('tmp/nx-lut-lite/eye-af.resume-ready','999\n'); self.assertNotEqual(self.run_wake().returncode, 0)

if __name__ == '__main__': unittest.main()
