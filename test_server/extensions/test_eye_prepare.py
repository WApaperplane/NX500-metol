"""Prepare-only installer with real pinned inputs in a temporary fake root."""
import os
from pathlib import Path
import subprocess
import tarfile
import tempfile
import unittest

UNIT_REL = "usr/lib/systemd/system/di-camera-app.service"
T2_UNIT_MEMBER = UNIT_REL                                   # 371B，带 ExecStartPre
STOCK_UNIT_MEMBER = "opt/usr/nx-ks/dicam/di-camera-app.service.p5.bak"  # 321B，原厂


def _can_symlink():
    with tempfile.TemporaryDirectory() as temp:
        try:
            (Path(temp) / "probe").symlink_to("target")
        except (OSError, NotImplementedError):
            return False
    return True


# install 路径里 ln -s 是硬步骤（systemd 的 hib_post.service.wants 必须是符号链接）。
# 宿主没这个权限时不能假装跑过：TEST_FAIL_WANTS 用例会因 ln 先失败而"通过"，
# 看上去绿了却根本没走到 Wants 门禁——那比跳过硬得多。
NEED_SYMLINK = unittest.skipUnless(
    _can_symlink(), "宿主不能建符号链接（Windows 需 SeCreateSymbolicLinkPrivilege/开发者模式）")


@unittest.skipUnless(os.environ.get("EYE_PREPARE_DIR"), "requires generated prepare package")
class PrepareTest(unittest.TestCase):
    def prepare_root(self, temp, stock_unit=False):
        root = Path(temp) / "root"
        pkg = Path(temp) / "pkg"
        pkg.mkdir()
        with tarfile.open(Path(os.environ["EYE_PREPARE_DIR"]) / "prepare.tar") as t:
            t.extractall(pkg, filter="data")
        with tarfile.open(os.environ["EYE_CONFIG_BACKUP"]) as t:
            # 机身基线二选一：装 T2 前置挂载的 unit，或它动手前的原厂 unit。
            # 两份都在同一个现场备份里，谁都不是凭空造的哈希。
            (root / UNIT_REL).parent.mkdir(parents=True, exist_ok=True)
            (root / UNIT_REL).write_bytes(
                t.extractfile(STOCK_UNIT_MEMBER if stock_unit else T2_UNIT_MEMBER).read())
            for name in ("opt/usr/nx-ks/dicam/t2_premount.sh",
                         "opt/usr/nx-ks/dicam/di-camera-app.c2"):
                dest = root / name
                dest.parent.mkdir(parents=True, exist_ok=True)
                dest.write_bytes(t.extractfile(name).read())
        for name, data in {"etc/version.info": "1.12\nNX500\n",
                           "sys/class/extcon/USB/state": "0\n",
                           "proc/swaps": "Filename Type Size Used Priority\n",
                           "proc/sys/kernel/random/boot_id": "test-boot\n"}.items():
            dest = root / name
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_text(data)
        fakebin = Path(temp) / "bin"
        fakebin.mkdir()
        bb = root / "opt/usr/nx-ks/busybox"
        bb.write_text('#!/bin/sh\ncmd=$1;shift;exec "$cmd" "$@"\n')
        bb.chmod(0o755)
        mount = fakebin / "mount"
        mount.write_text('#!/bin/sh\nexit 0\n')
        mount.chmod(0o755)
        ctl = fakebin / "systemctl"
        # ExecStartPre 必须单独回答：drop-in 只清 ExecStart，前置挂载要原样留着，
        # 拿 ExecStart 的输出去顶会让安装后那条实证恒假。
        ctl.write_text('#!/bin/sh\ncase "$*" in\n'
                       '*ExecStartPre*) echo "ExecStartPre={ path=/opt/usr/nx-ks/dicam/t2_premount.sh ; argv[]=/opt/usr/nx-ks/dicam/t2_premount.sh ; ignore_errors=yes ; }";;\n'
                       '*ExecStart*) echo "ExecStart={ argv[]=/bin/sh /usr/local/lib/nx-eye-af/launch.sh ; }";;\n'
                       '*Wants*) if [ "$TEST_FAIL_WANTS" = 1 ]; then echo Wants=; else echo Wants=nx-eye-resume.service;fi;;\n'
                       'esac\n')
        ctl.chmod(0o755)
        # 必须 as_posix()：把 Windows 原生反斜杠写进 shell 脚本会被当转义符吃掉
        # （C:\Users 变成 C:Users）⇒ grep/test 全在找不存在的路径。
        ctl.write_text(ctl.read_text().replace('/usr/local', root.as_posix() + '/usr/local'))
        script = (pkg / "prepare.sh").read_text()
        # Redirect only device namespaces, preserving /bin/sh and host tools.
        for prefix in ("/usr/local", "/usr/lib/systemd", "/opt/usr", "/etc/systemd", "/etc/version.info", "/sys/class", "/proc/"):
            script = script.replace(prefix, root.as_posix() + prefix)
        (pkg / "prepare.sh").write_text(script)
        env = dict(os.environ, PATH=str(fakebin) + ":" + os.environ["PATH"])
        return root, pkg, env

    def run_fixture(self, fail_wants=False):
        with tempfile.TemporaryDirectory() as temp:
            root, pkg, env = self.prepare_root(temp)
            env["TEST_FAIL_WANTS"] = "1" if fail_wants else "0"
            checked = subprocess.run(["sh", str(pkg / "prepare.sh"), "check"], env=env, capture_output=True, text=True)
            self.assertEqual(checked.returncode, 0, checked.stdout + checked.stderr)
            result = subprocess.run(["sh", str(pkg / "prepare.sh"), "install"], env=env, capture_output=True, text=True)
            if fail_wants:
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse((root / "usr/local/lib/nx-eye-af").exists())
                self.assertFalse((root / "etc/systemd/system/di-camera-app.service.d").exists())
            else:
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn("PREPARED_NOT_ARMED", result.stdout)
                self.assertIn("BASELINE=t2", result.stdout)
                self.assertFalse((root / "opt/usr/nx-eye-af/armed").exists())
                again = subprocess.run(["sh", str(pkg / "prepare.sh"), "install"], env=env, capture_output=True, text=True)
                self.assertNotEqual(again.returncode, 0)
                backup = next(line[7:] for line in result.stdout.splitlines() if line.startswith("BACKUP="))
                status = (Path(backup) / "status").read_text()
                self.assertIn("baseline=t2", status)
                withdrawn = subprocess.run(["sh", str(pkg / "prepare.sh"), "rollback", backup], env=env, capture_output=True, text=True)
                self.assertEqual(withdrawn.returncode, 0, withdrawn.stdout + withdrawn.stderr)
                self.assertFalse((root / "usr/local/lib/nx-eye-af").exists())
                self.assertTrue((Path(backup) / "runtime.withdrawn").exists())

    @NEED_SYMLINK
    def test_prepare(self):
        self.run_fixture()

    @NEED_SYMLINK
    def test_rollback_on_service_mismatch(self):
        self.run_fixture(True)

    def test_stock_body_refused(self):
        """原厂 unit（无 T2 前置挂载）必须被点名拒绝，而不是冒充 SERVICE_CHANGED。

        T2 两个文件在不在，证明不了它接没接进启动链；只有 unit 正文能证明。
        原厂 unit 上装 EYE = 让 EYE 跑在 55dcc781 的原厂 app 上，不是审计过的配置。
        """
        with tempfile.TemporaryDirectory() as temp:
            root, pkg, env = self.prepare_root(temp, stock_unit=True)
            env["TEST_FAIL_WANTS"] = "0"
            for mode in ("check", "install"):
                result = subprocess.run(["sh", str(pkg / "prepare.sh"), mode], env=env, capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0, mode)
                self.assertIn("RESULT=T2_NOT_WIRED", result.stderr, mode)
            self.assertFalse((root / "usr/local/lib/nx-eye-af").exists())
            self.assertFalse((root / "etc/systemd/system/di-camera-app.service.d").exists())
            self.assertFalse((root / "opt/usr/nx-eye-af").exists())


if __name__ == "__main__":
    unittest.main()
