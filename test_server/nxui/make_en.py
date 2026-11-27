#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""make_en.py -- 从 nxui.c 生成英文源码 out/nxui_en.c（只替换字符串字面量）。

为什么用"生成"而不是在源码里加 #ifdef：
  * nxui.c 有 135 条用户可见中文，散在 700 行里，改 #ifdef 会污染每一处；
  * 生成器能机械校验"是否还有漏翻的中文"，翻译单独放 i18n/en.json，改文案不动 C；
  * 注释里的中文不受影响（只替换带引号的字面量）。

★ 2026-11-27：扫描器改为先遮蔽注释，再找字面量。
  旧版是"全文找引号对"，注释里举例的 "<文字>\\t<秒>" 也会被当成待翻译条目，
  每加一条带引号的注释就要在 en.json 里凭空补一条翻译。现在注释里的引号
  一律不参与替换（与上面"注释保留中文"的承诺一致）。

用法：
  python3 make_en.py            # 生成 out/nxui_en.c 并报告
  python3 make_en.py --check    # 只检查覆盖率，不写文件
"""
import json
import pathlib
import re
import sys

HERE = pathlib.Path(__file__).resolve().parent
SRC = HERE / "nxui.c"
TABLE = HERE / "i18n" / "en.json"
OUT = HERE / "out" / "nxui_en.c"

CJK = re.compile("[\u4e00-\u9fff]")
LIT = re.compile(r'"((?:[^"\\]|\\.)*)"')


def unescape(s: str) -> str:
    return s.replace('\\"', '"').replace("\\\\", "\\")


def escape(s: str) -> str:
    return s.replace("\\", "\\\\").replace('"', '\\"')


def mask_comments(src: str) -> str:
    """把 // 与 /* */ 注释整段换成空格（换行保留），长度与原文一一对应。

    字符串与字符字面量原样跳过，所以 "http://x" 或单引号不会把扫描带偏。
    """
    out = list(src)
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if c == "/" and i + 1 < n and src[i + 1] == "/":
            j = src.find("\n", i)
            j = n if j < 0 else j
            for k in range(i, j):
                out[k] = " "
            i = j
        elif c == "/" and i + 1 < n and src[i + 1] == "*":
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            for k in range(i, j):
                if src[k] != "\n":
                    out[k] = " "
            i = j
        elif c in "\"'":
            quote = c
            i += 1
            while i < n:
                if src[i] == "\\":
                    i += 2
                    continue
                if src[i] == quote:
                    i += 1
                    break
                i += 1
        else:
            i += 1
    return "".join(out)


def main() -> int:
    src = SRC.read_text(encoding="utf-8")
    table = json.loads(TABLE.read_text(encoding="utf-8"))

    # 注释先遮蔽：长度不变，所以下面可以直接用 match 的偏移去切原文。
    masked = mask_comments(src)
    missing, used, out = [], set(), []
    prev = 0

    def repl(m: re.Match) -> str:
        text = unescape(m.group(1))
        if not CJK.search(text):
            return m.group(0)
        if text not in table:
            missing.append((text, masked[: m.start()].count("\n") + 1))
            return m.group(0)
        used.add(text)
        return '"' + escape(table[text]) + '"'

    for m in LIT.finditer(masked):
        out.append(src[prev:m.start()])
        out.append(repl(m))
        prev = m.end()
    out.append(src[prev:])
    out_text = "".join(out)

    if missing:
        print("!! 缺翻译 %d 条：" % len(missing))
        for s, line in missing:
            print("    nxui.c:%d  %s" % (line, s))
        return 2

    # 断言：剩下的 CJK 只允许出现在注释里（注释保留中文，不影响编译产物）
    rest = mask_comments(out_text)                    # 注释换空格
    rest = LIT.sub('""', rest)                        # 字面量换空串
    if CJK.search(rest):
        hit = CJK.search(rest)
        line = rest[: hit.start()].count("\n") + 1
        print("!! 第 %d 行仍有字面量外的中文（多半是新加的字符串没进 en.json）" % line)
        return 3

    unused = sorted(set(table) - used)
    print("替换 %d 条；表内未用 %d 条" % (len(used), len(unused)))
    for s in unused[:10]:
        print("   未用:", s)

    if "--check" in sys.argv:
        return 0

    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text("/* 由 make_en.py 从 nxui.c 生成，勿手改。文案见 i18n/en.json */\n" + out_text,
                   encoding="utf-8")
    print("已写:", OUT)
    return 0


if __name__ == "__main__":
    sys.exit(main())
