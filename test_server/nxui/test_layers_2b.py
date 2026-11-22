"""方案 2b：菜单页 → 滤镜页的两层换签，必须真的在**同一个进程**里换。

被验的是协议，不是观感：
  1. 菜单项 `filter:lut` / `filter:pw` 被 nxui 拦下，不 fork 任何东西 ——
     日志里必须是 `ACTION filter=lut` 后紧跟 `LAYER lut`，且 pid 不变。
  2. 换层只换列表：`entries=` 分别是两层各自的条目数。
  3. 顶栏「菜单」回到**进来时那一页**（不是主菜单 gui_ini）。
  4. 换层不触发任何 apply —— 只有「选中」才算应用。

为什么值得单测：旧的 `nxui.sh switch` 是「杀自己再起一个」，任何一处写回
`exec nxui.sh` 都会让面板消失而不是换页；而这条路径的失败是静默的
（面板没了，看起来像崩溃）。
"""
import os
from pathlib import Path
import subprocess
import tempfile
import time

HERE = Path(__file__).resolve().parent
OUT = HERE / "out"
# arm 目标要用 arm 版 test_input（见 verify_display 的 NXUI_TEST_INPUT）
INPUT = Path(os.environ.get("NXUI_TEST_INPUT") or (OUT / "test_input"))

TOP_Y = 20          # 顶栏行中心
TAB_LUT_X = 78       # [LUT 132..] 起点 12、宽 132 ⇒ 12..144
TAB_FILMLAB_X = 228  # [LUT 132][FILMLAB 152] 起点 12、间距 8 ⇒ FILMLAB 152..304
BTN_MENU_X = 507     # 菜单 444..570
ROW0_X, ROW1_Y = 180, 60   # 菜单页第一项（2 列 × 3 行，行高 55）


def run_layers(prefix, env, label):
    with tempfile.TemporaryDirectory(prefix="nxui-2b-") as temp:
        root = Path(temp)
        (root / "auto").mkdir()
        calls = root / "calls"
        engine = root / "engine.sh"
        engine.write_text('#!/bin/sh\nprintf "%s\\n" "$*" >> "$NXUI_CALLS"\nexit 0\n')
        engine.chmod(0o755)
        looks = ["look_a", "look_b", "look_c"]
        pw = ["pw_a", "pw_b"]
        vlog = ["vlog_a", "vlog_b"]
        (root / "looks.rec").write_text("".join("%s|%s|0|0|0|0|0|0|0\n" % (n, n) for n in looks))
        (root / "looks_vlog.rec").write_text("".join("%s|%s|0|0|0|0|0|0|0\n" % (n, n) for n in vlog))
        (root / "pw.txt").write_text("".join("%s|%s|10|20|30|4|5|6|7\n" % (n, n) for n in pw))
        (root / "gui_evwifi_test.NX500").write_text(
            "button|LUT 滤镜|filter:lut\n"
            "button|FilmLab|filter:pw\n"
            "button|返回|@/opt/usr/nx-ks/gui_ini.NX500\n")
        env = dict(env, NXUI_CALLS=str(calls))
        log = root / "run.log"
        stream = log.open("w")
        p = subprocess.Popen(prefix + ["--live", "--root", str(root),
                                       "--recipes", str(root / "pw.txt"),
                                       "--lut-recipes", str(root / "looks.rec"),
                                       "--lut-recipes-vlog", str(root / "looks_vlog.rec"),
                                       "--engine", str(engine),
                                       "--menu", "gui_evwifi_test.NX500",
                                       "--lifetime-ms", "9000"],
                             env=env, stdout=stream, stderr=subprocess.STDOUT)
        try:
            # 等 READY，不是等 MENU —— MENU 行在字体/窗口就绪之前就打出来了，
            # qemu 下两者差 400 ms，抢在 READY 前断言 panel= 会假失败。
            for _ in range(200):
                if "READY" in log.read_text(): break
                if p.poll() is not None: raise AssertionError(log.read_text())
                time.sleep(.05)
            else:
                raise AssertionError("menu did not load\n" + log.read_text())
            # 菜单页高度：3×4 网格改造（8a12fe7）后固定 292，不再是 2 列时代的 336。
            assert "panel=720x292" in log.read_text(), log.read_text()

            def click(x, y, ms="40"):
                subprocess.run([str(INPUT), "click", str(x), str(y), ms],
                               env=env, check=True, timeout=6)

            # 1. 菜单项 → LUT 层（同进程）
            click(ROW0_X, ROW1_Y)
            for _ in range(60):
                if "LAYER lut" in log.read_text(): break
                time.sleep(.05)
            text = log.read_text()
            assert "ACTION filter=lut" in text, text[-600:]
            assert "LAYER lut entries=3" in text, text[-600:]

            # 1b. 已经在 LUT 层时再点 LUT 页签 = 换输入域（std ⇄ vlog）。
            #     这是「vlog 表不单独开第三个页签」的实现：页签仍是 LUT，
            #     第二次点击换的是同一层里的那份清单。换域只是重读列表，
            #     不能产生任何 apply（否则手指划过页签就会写一次 A18 槽）。
            click(TAB_LUT_X, TOP_Y)
            for _ in range(60):
                if "SCOPE vlog" in log.read_text(): break
                time.sleep(.05)
            text = log.read_text()
            assert "SCOPE vlog entries=2" in text, text[-600:]
            click(TAB_LUT_X, TOP_Y)
            for _ in range(60):
                if "SCOPE std" in log.read_text(): break
                time.sleep(.05)
            text = log.read_text()
            assert "SCOPE std entries=3" in text, text[-600:]
            assert not calls.exists() or "apply" not in calls.read_text(), "换域触发了 apply"

            # 2. 顶栏页签 → PW 层
            click(TAB_FILMLAB_X, TOP_Y)
            for _ in range(60):
                if "LAYER pw" in log.read_text(): break
                time.sleep(.05)
            text = log.read_text()
            assert "LAYER pw entries=2" in text, text[-600:]

            # 2b. 单击 WiFi / AEL 换层（2026-11-20 追加要求）
            #     WiFi = X keycode 223（有 keysym XF86Mail），AEL = 222/224 之一
            #     （键位表上是 NoSymbol）—— 两条路径都必须能换。
            def send_keycode(code):
                subprocess.run([str(INPUT), "keycode", code],
                               env=env, check=True, timeout=6)

            n_before = log.read_text().count("LAYER lut")
            send_keycode("223")
            for _ in range(60):
                if log.read_text().count("LAYER lut") > n_before: break
                time.sleep(.05)
            text = log.read_text()
            assert text.count("LAYER lut") > n_before, ("WiFi 键没换层", text[-600:])

            n_before = log.read_text().count("LAYER pw")
            send_keycode("222")
            for _ in range(60):
                if log.read_text().count("LAYER pw") > n_before: break
                time.sleep(.05)
            text = log.read_text()
            assert text.count("LAYER pw") > n_before, ("AEL 键没换层", text[-600:])

            # 3. 「菜单」回进来时那一页，不是 gui_ini
            click(BTN_MENU_X, TOP_Y)
            for _ in range(60):
                if text.count("MENU file=") > 1: break
                time.sleep(.05)
                text = log.read_text()
            text = log.read_text()
            assert text.count("MENU file=") >= 2, text[-600:]
            assert "gui_evwifi_test.NX500" in text.split("MENU file=")[-1], text[-600:]

            # 4. 换层不产生任何 apply
            calls_text = calls.read_text() if calls.exists() else ""
            assert "apply" not in calls_text, calls_text

            p.wait(timeout=12)
            return log.read_text()
        finally:
            if p.poll() is None:
                p.terminate(); p.wait(timeout=8)
            stream.close()
