import os, re, sys, json

CUR = 'docs/current'

GROUPS = [
 ("A2_PATCH_AND_READBACK_2026-10-14.md", "A2 补丁：设计、槽写前置与回读判定",
  ["A2_PATCH_DESIGN_2026-10-14.md", "A2_SLOT_WRITE_PREREQ_2026-10-14.md", "A2_READBACK_VERDICT_2026-10-15.md"]),
 ("MCB_PROTOCOL_AND_ENUM_2026-10-08.md", "MCB 协议与命令枚举表",
  ["MCB_COMMAND_ENUM_TABLE_2026-10-08.md", "MCB_PROTOCOL_AND_ST_TABLE_2026-10-08.md"]),
 ("TC_CURVE_AND_RESTORE_2026-11-08.md", "TC 曲线：19 节点表与还原工作流",
  ["TC_CURVE_ONDEVICE_2026-11-08.md", "TC_RESTORATION_WORKFLOW_2026-11-08.md"]),
 ("DUAL_LUT_AND_MIX_2026-10-31.md", "双表可行性与强度混合",
  ["DUAL_LUT_FEASIBILITY_2026-10-31.md", "DUAL_STRENGTH_2026-11-02.md"]),
 ("CUBE2LUT_CONVERTER_2026-10-25.md", "机内 cube→LUT 转换器：语义与修正",
  ["CUBE2LUT_2026-10-18.md", "CUBE2LUT_NATIVE_FIX_2026-10-25.md"]),
 ("RUNTIME_ARCH_AND_P0_2026-11-16.md", "运行态架构与 P0 验收",
  ["RUNTIME_ARCHITECTURE_DESIGN_2026-11-16.md", "RUNTIME_P0_ACCEPTANCE_2026-11-16.md"]),
 ("T2_DELIVERY_AND_PRESET_2026-10-16.md", "T2：交付修订与预置协议",
  ["T2_DELIVERY_REVISION_2026-10-15.md", "T2_PRESET_PROTOCOL_2026-10-16.md"]),
 ("FEATURE_MATRIX_FULL_2026-10-11.md", "功能矩阵（含 mod 全量版）",
  ["FEATURE_MATRIX_2026-10-07.md", "FEATURE_MATRIX_MOD_FULL_2026-10-11.md"]),
 ("VIDEO_LUT_AND_10BIT_2026-11-07.md", "视频：首帧 LUT、2.5K 与 10bit 可行性",
  ["VIDEO_LUT_FIRSTFRAME_2026-11-07.md", "VIDEO_LUT_AND_2K_2026-11-07.md", "VIDEO_10BIT_FEASIBILITY_2026-11-07.md"]),
 ("CURVE_ANATOMY_2026-11-17.md", "曲线解剖：调用链、DDR 布局与插值",
  ["CURVE_CALLLINK_2026-11-17.md", "CURVE_DDR_LAYOUT_2026-11-17.md", "CURVE_INTERP_2026-11-17.md"]),
 ("3DLUT_PATHS_FORMAT_ISOLATION_2026-10-08.md", "3D LUT：生效路径、表格式与 View/Still 隔离",
  ["3DLUT_APPLY_PATHS_2026-10-08.md", "3DLUT_TABLE_FORMAT_2026-10-08.md", "3DLUT_VIEW_STILL_ISOLATION_2026-10-08.md"]),
 ("NXUI_UI_DETAILS_2026-10-19.md", "NXUI 交互细节：首帧、启动、面板与滚动",
  ["NXUI_FIRST_FRAME_2026-10-19.md", "NXUI_LAUNCH_APPLET_2026-10-19.md", "NXUI_PANEL_UX_2026-10-19.md", "NXUI_SCROLLBAR_2026-10-19.md"]),
]


def split_h1(text):
    lines = text.splitlines()
    title, start = None, 0
    for i, ln in enumerate(lines):
        if ln.startswith('# '):
            title, start = ln[2:].strip(), i + 1
            break
        if ln.strip() and not ln.startswith('>'):
            break
    return title, '\n'.join(lines[start:]).strip('\n')


def demote(body):
    out, fence = [], False
    for ln in body.splitlines():
        if ln.strip().startswith('```'):
            fence = not fence
            out.append(ln); continue
        if not fence and re.match(r'^#{1,5} ', ln):
            ln = '#' + ln
        out.append(ln)
    return '\n'.join(out)


mapping = {}
report = []
for newname, totals, members in GROUPS:
    secs, toc = [], []
    for idx, m in enumerate(members, 1):
        raw = open(os.path.join(CUR, m), encoding='utf-8', errors='ignore').read()
        h1, body = split_h1(raw)
        h1 = h1 or m
        body = demote(body)
        for j, other in enumerate(members, 1):
            if other == m:
                continue
            body = body.replace('`%s`' % other, '本文 §%d' % j).replace(other, '本文 §%d' % j)
        toc.append('%d. %s' % (idx, h1))
        secs.append('## %d. %s\n\n> 来源：`%s`（已归档至 `docs/archive/`）\n\n%s' % (idx, h1, m, body))
        mapping[m] = newname
    head = ('# %s\n\n> **2026-11-20 文档合并：** 本文由 %d 份同主题记录合并而成，原文按时间顺序保留、未删改，'
            '标题层级下调一级。原始文件已归档到 `docs/archive/`，文件名见各节「来源」。\n\n%s\n\n---\n\n'
            % (totals, len(members), '\n'.join(toc)))
    open(os.path.join(CUR, newname), 'w', encoding='utf-8').write(head + '\n\n'.join(secs) + '\n')
    report.append((newname, len(members), sum(1 for _ in open(os.path.join(CUR, newname), encoding='utf-8'))))

for n, c, l in report:
    print('%-52s %3d 份 %5d 行' % (n, c, l))
json.dump(mapping, open('/tmp/merge_map.json', 'w'), ensure_ascii=False, indent=1)
print('合并 %d 份 → %d 份' % (len(mapping), len(GROUPS)))
