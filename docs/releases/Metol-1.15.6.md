# Metol 1.15.6

发布日期：2026-11-16
这一版只做一件事：把项目里残留的 **NX-WA / NXWA / 2.88** 代号全部清掉，对外只用 Metol。功能与 1.15.5 完全一致。

## 一、版本号改法

| | |
|---|---|
| 旧 | `2.88.0-nxwa.15.5`（NX-KS 血脉 + 序号 + 补丁） |
| 新 | `1.15.6`（主版本 + 序号 + 补丁） |

对应关系：

| 旧 | 新 |
|---|---|
| `2.88.0-nxwa.1` … `2.88.0-nxwa.15` | **1.1.0** … **1.15.0** |
| `2.88.0-nxwa.15.1` … `.15.4` | **1.15.1** … **1.15.4** |
| `2.88.0-nxwa.15.5` | **1.15.5** |

NX-KS 2.88 仍然是代码血缘（见 `VERSION` 的 bloodline 行与 `ATTRIBUTION.md`），只是不再进版本号。

## 二、改了哪些地方

| 层 | 内容 |
|---|---|
| 包名 | `NX-WA-<ver>.zip` → `Metol-<ver>.zip`；清单改名 `Metol-MANIFEST.txt` |
| 相机菜单 | 「版本 v2.88」→「Metol 版本」；「卸载模块 v2.88」→「卸载模块」。NX500 / NX1 四份中文菜单 + 两份英文覆盖层全部同步 |
| 版本弹窗 | `ver.sh` 改为先从包内 `VERSION` 读版本号再弹，以后升版不用动脚本；读不到时回退到内置值 |
| 文档 | `README.md` / `README_EN.md` 标题、最新版本行、包名说明；`ATTRIBUTION.md` 标题 |
| 运行时 | `/tmp/nxwa-tc.lock` → `/tmp/metol-tc.lock`；`/tmp/nxwa-lut-video` → `/tmp/metol-lut-video`；`/tmp/nxwa-pwrlog.{pid,lock}` → `/tmp/metol-pwrlog.*`。都在 tmpfs，重启即清，无迁移成本 |
| 注释 | `f.sh`、`nxstore.sh`、`pwrlog.sh`、`init.sh` 里的 NX-WA / NXWA / 2.88 |

## 三、有意保留 nxwa 字样的地方

| 位置 | 原因 |
|---|---|
| `/mnt/mmc/nxwa_regtab_20261018` | `nx_regtabwrite.arm` 内硬编码该路径。只改 shell 会让备份与读取落到两个目录，改名必须同步重编二进制 |
| `/tmp/nxwa_lutload.lock`、`/tmp/nxwa_lutwatch.lock` | 同上，`nx_lutload.arm` 内硬编码 |
| git 分支 `nxwa` | 改名会让所有既有克隆与 PR 指错，收益为零 |
| 历史 tag 与 release 的 tag | GitHub tag 不可修改 |
| `docs/archive/`、`docs/releases/` 旧版、`raw8/` | 历史记录，改写会破坏可追溯性 |
| `ssh-ed25519 … xiaoa@nxwa` | SSH 公钥注释，与项目命名无关 |

上述三处硬编码路径会随下一次二进制重编一起处理。

## 四、历史 release 只改标题

GitHub 上 18 个 `NX-WA 2.88.0-nxwa.X` 的 release 标题已改为 `Metol 1.X.Y`，正文首行同步。**tag 与包文件名全部保持原样**——它们是不可变的历史存档，重命名会让既有下载链接失效，收益为零。新包从 1.15.6 起才用 `Metol-` 前缀。

## 五、包

| 包 | 大小 | md5 | 条目 |
|---|---|---|---|
| `Metol-1.15.6.zip` | 10,512,313 B | `352b40b75120243d9c3d80c9f82a53ef` | 401 + manifest |
| `Metol-1.15.6-EN.zip` | 10,511,510 B | `470caec6edc97c76bebee3b664410ef8` | 401 + manifest |

## 六、升级

与以往相同：解压到 SD 卡根 → 插卡等待自动同步 → 重启。本次无功能性改动，可以跳过；升级后相机菜单里那一项会显示「Metol 版本」，点开第一屏就是当前版本号。
