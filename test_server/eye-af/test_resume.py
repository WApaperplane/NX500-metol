import pathlib, subprocess, tempfile, unittest

WAKE = pathlib.Path(__file__).resolve().parents[2] / 'contrib/nx500-eye-af/metol/wake.sh'
# 库路径必须落在 --test-root 里：wake.sh 现在按 $root 前缀探测，真实相机上
# root 为空时退化成 /usr/local/lib/...，两者语义一致。
LIB_REL = 'usr/local/lib/nx-eye-af/nxl-eye-af-yunet.so'
APP = '/usr/apps/com.samsung.di-camera-app/bin/di-camera-app'

class Resume(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(); self.root = pathlib.Path(self.tmp.name)
        self.put(LIB_REL, 'fake library')
        self.lib = self.root.as_posix() + '/' + LIB_REL
        self.put('sys/class/extcon/USB/state', '0\n'); self.put('sys/power/bootmode', '2\n')
        self.put('tmp/.hib_post', ''); self.put('proc/swaps', 'Filename Type Size Used Priority\n')
        self.put('proc/sys/kernel/random/boot_id', 'new\n'); self.put('test-dmesg', 'kernel_resume start!\n')
        self.put('opt/usr/nx-eye-af/armed', 'old\nEYE\n'); self.put('opt/usr/nx-eye-af/capture-origin', 'old\nEYE\n')
        self.put('opt/usr/nx-eye-af/launch-claim', f'boot_id=new\npolicy=EYE\ncamera_pid=123\nfile={self.lib}\nmapped_dev=01:02\nmapped_ino=42\n')
        self.put('proc/123/maps', f'100-200 r-xp 0 01:02 42 {self.lib}\n')
        exe = self.root / 'proc/123/exe'
        try:
            exe.symlink_to(APP)
        except OSError as exc:
            # Windows 非管理员没有 SeCreateSymbolicLinkPrivilege；wake.sh 在 --test-root
            # 下认"内容是目标路径的普通文件"，真机仍只认真正的符号链接。
            if getattr(exc, 'winerror', None) != 1314:
                raise
            exe.write_text(APP)
    def tearDown(self): self.tmp.cleanup()
    def put(self, name, text):
        p = self.root / name; p.parent.mkdir(parents=True, exist_ok=True)
        # 必须强制 LF：Windows 下 write_text 默认把 \n 翻成 \r\n，而 wake.sh 的
        # `read -r policy` 会把 \r 当成内容字节 ⇒ policy='EYE\r' ⇒ 恒 GENERATION_MISMATCH。
        p.write_text(text, newline='\n')
    def run_wake(self):
        # 测试根必须用正斜杠传：claim 里记的 file= 要与 sh 里 `$root/usr/...` 拼出来的
        # 字符串逐字节相等，pathlib 的 Windows 原生形式是反斜杠，会恒对不上。
        return subprocess.run(['sh', str(WAKE), '--test-root', self.root.as_posix()], capture_output=True, text=True)
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
