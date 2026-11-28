# EYE28 · 部署侧布置（未上机）

生成 2026-11-27 · 机身：NX500 1.12 + NX-KS2 主模块 + Log117 gamma 树
状态：**PC 侧完成，机身未写入任何文件**

---

## 这套是什么关系

`contrib/nx500-eye28/`（Terra，17:17–17:42）是**离线安全审查包**：算法原版（`e167e84a`，
上游 `eafc274`）、部署硬禁用、`eye28-verify.arm` 无论传什么都输出 `NATIVE`、
`runtime/launch.sh` 只是选择器模型不 exec App。SAFETY_REVIEW 明确列了四条设备门禁未过。

本目录补的是它缺的**部署侧**：把同一份库接进已在 PC 回放验证过的注入/恢复/守卫骨架。

| 层 | 来源 |
|---|---|
| 库 `nxl-eye-af.so` | `contrib/nx500-eye28/out/`，`e167e84a…`，与上游逐字节一致 |
| `launch.sh` / `wake.sh` | `contrib/nx500-eye-af/metol/`，改为按文件探测后端 |
| `claim.arm` | `contrib/nx500-eye-af/out/`（`c2f141a7…`） |
| prepare / guards | `tools/extensions/build_eye_prepare.py`、`build_eye_capture.py` |

**没有做**：没有把 `eye28-verify.arm` 接进来。它是审查用 helper（生产恒返回 `NATIVE`），
接进启动链等于自欺。注入判定仍走 `claim.arm` 的实际 maps 校验。

---

## 两个后端，一套骨架

`build_metol_bundle.py --lib <库>` 按文件名选 profile：

| 库 | profile | 载荷文件数 | 说明 |
|---|---|---|---|
| `nxl-eye-af.so` | `eye28-native` | 8 | 上游原版算法（本目录用的） |
| `nxl-eye-af-yunet.so` | `eye-yunet32` | 10 | 加 32×32 INT8 YuNet，多两个 Notice |

`launch.sh`/`wake.sh` 不再写死库名，按 `nxl-eye-af.so` → `nxl-eye-af-yunet.so` 顺序探测，
`verify-payload.sh` 只 pin 实际装的那一个。

---

## 目录

```
00-payload/    8 文件 + manifest.json：nxl-eye-af.so(e167e84a)、claim.arm、launch.sh、
               wake.sh、nx-eye-resume.service、conf 模板、LICENSE、verify-payload.sh
10-prepare/    prepare.sh + prepare.tar
20-guards/     9 文件守卫包（hibernation 双段门禁、guard、arm-rescue、battery-gate、
               withdraw、install）
30-verify/     check-before-install.sh（只读预检）
```

---

## 上机顺序（现在不要执行）

### 0 · 只读预检，必须 `READY_FOR_PREPARE`

```sh
cd /e/NXWA/nx500-modding-work
export NXHOST=192.168.0.104
bash tools/nx-ssh.sh push /e/NXWA/_deliverables/eye28-20261127 /mnt/mmc/
bash tools/nx-ssh.sh cmd '
D=/mnt/mmc/eye28-20261127
sync; sleep 2
chmod 755 "$D"/10-prepare/prepare.sh "$D"/20-guards/*.sh "$D"/30-verify/*.sh
sh "$D/30-verify/check-before-install.sh"
'
```

判据：`UNIT_PIN`(ac1bab44) / `T2_PIN`(99931046) / `APP_COPY_PIN`(ec395123) / `APP_BINARY` /
`NO_FOOTPRINT` / `HEADER_WRITER`(dd3687d3) / `FACTORY_WRITER`(08df6e37) / `USB_CLEAR` / `SNAPSHOT_IDLE`。
任何 `FAIL` 停手。

### 0b · 两条机身基线（11-28 更正）

`di-camera-app.service` 在本机上出现过**两个**真实版本，都在
`rea-evidence/20261126-eye-log117/startup-before.tar` 里，谁都谈不上"没有现场出处"：

| sha256 | 字节 | 正文 | 含义 |
|---|---|---|---|
| `ac1bab44…` | 371 | 原厂 + `ExecStartPre=-/opt/usr/nx-ks/dicam/t2_premount.sh` | **T2 已接入启动链**（本交付要求的基线） |
| `5f7824af…` | 321 | 原厂，无 `ExecStartPre` | 逐字节等于 `dicam_p5_install.sh` 之前的 `di-camera-app.service.p5.bak` |

`t2_premount.sh` 把 `/opt/usr/nx-ks/dicam/di-camera-app.c2`（`ec395123`）bind 到真实
`di-camera-app` 上。所以 premount 在不在，决定 `launch.sh::eligible()` 看到的是 `ec395123`（审计过的
c2 体）还是 `55dcc781`（原厂体）——**T2 的两个文件指纹只能证明文件在，证明不了它接进了启动链。**

`prepare.sh` 因此改成：`ac1bab44` → `baseline=t2` 放行；`5f7824af` → `RESULT=T2_NOT_WIRED`
点名拒绝（不再冒充 `SERVICE_CHANGED`）；其他 → `SERVICE_CHANGED`。装完再落一次实证——
`systemctl show -p ExecStartPre` 里 `t2_premount.sh` 必须还在（drop-in 只清 `ExecStart`），
否则 `T2_PREMOUNT_LOST` 并整体回滚。

> 11-28 实测：机身当前就是 `5f7824af`（`grep -c t2_premount` = 0，`.p5.bak` 已不在，
> T2 两个文件仍在 `/opt/usr/nx-ks/dicam/`）。要装 EYE，先跑 `dicam_p5_install.sh` 把前置挂载接回去。

### 1 · prepare（只改文件）

`prepare.sh` 判 `/mnt/mmc/eye28-20261127/10-prepare/payload/verify-payload.sh`，而 payload 在
`prepare.tar` 里、不在同级目录。**直接 `sh prepare.sh install` 必报 `RESULT=UNTRUSTED_VERIFIER`**
（不是包坏了，是没解包）。先解到临时目录再从那里跑：

```sh
bash tools/nx-ssh.sh cmd '
BB=/opt/usr/nx-ks/busybox
T=/mnt/mmc/_eye-prep; $BB rm -rf "$T"; $BB mkdir -p "$T"
cd "$T" && $BB tar xf /mnt/mmc/eye28-20261127/10-prepare/prepare.tar
sh prepare.sh install
'
```

期望 `RESULT=PREPARED_NOT_ARMED` + `BASELINE=t2`，备份 `status` 里也记 `baseline=t2`。
若报 `T2_NOT_WIRED`：机身是原厂 unit，先跑 `dicam_p5_install.sh` 把前置挂载接回去再重来。
装完可 `$BB rm -rf /mnt/mmc/_eye-prep`（payload 已由 prepare 拷到 `/usr/local/lib/nx-eye-af`）。

### 2 · guards（仍然不动快照）

```sh
bash tools/nx-ssh.sh cmd 'sh /mnt/mmc/eye28-20261127/20-guards/install-guards.sh'
```

期望 `RESULT=GUARDS_INSTALLED_NOT_ARMED`。要求写入器是原厂，顺序不能反。

### 3 · 只读核验

| 项 | 期望 |
|---|---|
| `/usr/local/lib/nx-eye-af/nxl-eye-af.so` | `e167e84a…` |
| prepare status | `phase=PREPARED_NOT_ARMED` |
| guards status | `phase=GUARDS_INSTALLED_NOT_ARMED` |
| `systemctl show di-camera-app.service -p ExecStart` | 指向 `launch.sh` |
| `hib_post.service` Wants | 含 `nx-eye-resume.service` |

三步做完相机行为与装之前一致：`eligible()` 要求 `armed` 存在，没有就走原厂 `exec`。

---

## 11-28 上机实录（192.168.0.107，Metol 1.15.10 之后）

前置：先 `eye-withdraw.sh` 撤掉 03:46 那次旧包安装（`RESULT=WITHDRAWN`），再用
`dicam_p5_install.sh` 把 T2 前置挂载接回（unit → `ac1bab44`，`.p5.bak` 321 B），
然后重推重建过的 `eye28-20261127`（23 文件，`sha256sum -c` 22/22 OK）。

| 步骤 | 结果 |
|---|---|
| `30-verify/check-before-install.sh` | 全 PASS，`RESULT=READY_FOR_PREPARE`（`INFO bootmode=2`） |
| `10-prepare`（解包后） | `RESULT=PREPARED_NOT_ARMED` / `BASELINE=t2` / backup `nx-eye-prepare-20150102113044-1092` |
| `20-guards/install-guards.sh` | `RESULT=GUARDS_INSTALLED_NOT_ARMED`，7 文件全 OK |

只读核验实测：

- `nxl-eye-af.so` = `e167e84a…`；`verify-payload.sh` = `f145ad4e…`；`verify-payload.sh $rt` → `PAYLOAD_OK`
- `ExecStart` = `/bin/sh /usr/local/lib/nx-eye-af/launch.sh`；`ExecStartPre` = `t2_premount.sh`（`ignore_errors=yes`，drop-in 只清 `ExecStart` 的实证成立）
- `hib_post.service` `Wants=nx-eye-resume.service`；wants 软链 → `/etc/systemd/system/nx-eye-resume.service`
- `/opt/usr/nx-eye-af` 只有 `capture-writers-before/`，**无 `armed`、无 `launch-claim`**
- 写入器已换成守卫版：`hibernation.sh` `3779b8a1…`、`make_snapshot.sh` `ea12c6cf…`；
  备份里原厂对 `dd3687d3…` / `08df6e37…` 逐字节保留
- `/etc/systemd/system/hibernation.service.d/75-nx-eye-prewrite.conf` 已落
- 收尾 `/dev/root` 回到 `ro`（两次安装各自的 `trap … remount,ro` 都生效）

注意：`di-camera-app` 当前进程仍是旧 unit 拉起的；**`t2_premount.sh` 与 `launch.sh` 都要下次
正常关机→开机才真正进入启动链**。在 `armed` 不存在前 `eligible()` 为假，走原厂 `exec`，
所以这一状态与装之前行为一致。

---

## 仍未过的门禁（来自 SAFETY_REVIEW）

| # | 门禁 | 本目录状态 |
|---|---|---|
| 1 | 只读核实恢复后 App / T2 / libcapture / libface 与符号布局 | 未做（预检只核 App 与三个 pin） |
| 2 | 镜像头、快照代次与写入报告的可信关联 | 未做（守卫只保证拒绝时交回原厂） |
| 3 | 制作快照前的预加载接线与资源所有权回退 | **本目录补的就是这层**，但只在 PC 回放验证 |
| 4 | 备份与恢复路径核实后再做真实快启/休眠/对焦测试 | 未做 |

`RTLD_NEXT` 绑定失败返回 `-1` 的缺陷仍在上游算法里，不因本布置而消失。

---

## 校验整包

```sh
cd /e/NXWA/_deliverables/eye28-20261127 && sha256sum -c manifest.sha256
```

回滚：`sh 20-guards/withdraw-guards.sh` → `sh 10-prepare/prepare.sh rollback <backup>`。

---

## 11-28 修复：交付包与源码对齐

三处未闭环都在这一轮收掉：

| # | 问题 | 处理 |
|---|---|---|
| 1 | `_deliverables/eye28-20261127` 仍是旧安装器（旧服务哈希、写死 `nxl-eye-af-yunet.so`） | 由 `build_metol_bundle.py` + `build_eye_prepare.py` 从源码整包重建 `00-payload` / `10-prepare`；`manifest.sha256` 22/22 重算 |
| 2 | 服务哈希只认原厂 unit，却仍检查 T2 文件 | 见 §0b，两基线分别处置 + 装后 `ExecStartPre` 实证 |
| 3 | `test_resume` 在 `--test-root` 下仍探宿主机库路径 | `wake.sh` 的 `libroot` 改用 `$root` 前缀；夹具库建进临时根 |

重建顺带修掉两处已经漂移的文件：`00-payload/wake.sh` 落后源码一个提交，
`di-camera-app.conf.example` / `nx-eye-resume.service` 还带着 CRLF（相机 `sh` 会把 `\r`
当参数的一部分）。现在包内全部 LF。

PC 侧计数：EYE28 `test_server/eye28` **71/71**；扩展 `test_server/extensions` **32/32**
（`EYE_PREPARE_DIR` + `EYE_GUARDS_DIR` 开启后无跳过）；Eye AF `test_server/eye-af`
**6/6**（bundle/claim/resume×3/wireless）。跑在 WSL 里——Windows 宿主缺
`SeCreateSymbolicLinkPrivilege`，`ln -s` 建不出来，install 路径那两条会假绿，一律 skip 而非放过。
