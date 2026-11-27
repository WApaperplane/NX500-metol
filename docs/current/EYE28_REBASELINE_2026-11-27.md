# Eye AF 28 · 从原厂基线重新布置（未上机）

生成时间 2026-11-27 · 机身：NX500 1.12 + NX-KS2 主模块 + Log117 gamma 树
状态：**只做了 PC 侧准备，机身未写入任何文件**

---

## 这套东西是什么

刷机把 `/opt/usr` 上的 Eye 全清了（`/opt/usr/nx-eye-af`、`/usr/local/lib/nx-eye-af`、写入器守卫都不在），
所以不再走 r1→r5 的修补链，改成**从当前原厂基线一次装到位**。

| 目录 | 内容 | 谁生成 |
|---|---|---|
| `00-payload/` | 11 文件：`nxl-eye-af-yunet.so`(0955e5d9)、`claim.arm`、`launch.sh`、`wake.sh`、`nx-eye-resume.service`、conf 模板、License、`verify-payload.sh`、`manifest.json` | `contrib/nx500-eye-af/build_metol_bundle.py` |
| `10-prepare/` | `prepare.sh` + `prepare.tar` | `tools/extensions/build_eye_prepare.py` |
| `20-guards/` | 9 文件守卫包（hibernation 双段门禁、guard、arm-rescue、battery-gate、withdraw、install） | `tools/extensions/build_eye_capture.py` |
| `30-verify/` | `check-before-install.sh`（只读预检） | 本目录 |

**关键：payload 是从 `contrib/` 源码现打的，不是复用历史目录。**
`_deliverables/eye-yunet32-payload-20261126` 里是旧 `launch.sh`（`91498f19`），
现源码是 `8fa47d5c`，直接拿历史包会装错。

---

## 上机顺序

### 第 0 步 · 只读预检（必须 `READY_FOR_PREPARE`）

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

| 判据 | 含义 |
|---|---|
| `PASS UNIT_PIN` | `di-camera-app.service` 是 `ac1bab44…` |
| `PASS T2_PIN` | `t2_premount.sh` 是 `99931046…` |
| `PASS APP_COPY_PIN` | `di-camera-app.c2` 是 `ec395123…` |
| `PASS APP_BINARY` | App 是 `ec395123…` 或 `55dcc781…` |
| `PASS NO_FOOTPRINT` | 五个 Eye 路径都不存在 |
| `PASS HEADER_WRITER` / `FACTORY_WRITER` | 两个写入器是原厂 |
| `PASS USB_CLEAR` / `SNAPSHOT_IDLE` | USB=0、p9 不在 swap |

任何 `FAIL` 都不要往下走。`INFO` 行只是上下文（`GAMMA_TREE`、`METOL_BT_HOOK` 出现在预期之内）。

### 第 1 步 · 装 prepare（只改文件，不动快照）

```sh
bash tools/nx-ssh.sh cmd 'cd /mnt/mmc/eye28-20261127/10-prepare && sh prepare.sh install'
```

期望：`RESULT=PREPARED_NOT_ARMED` + `BACKUP=/opt/usr/nx-eye-prepare-<tag>`

失败码：`FIRMWARE` / `SERVICE_CHANGED` / `T2_SCRIPT_CHANGED` / `T2_APP_CHANGED` /
`PAYLOAD_MISMATCH` / `USB_CONNECTED` / `SNAPSHOT_BUSY` / `EXISTING_EYE_OR_OVERRIDE`

### 第 2 步 · 装写入守卫（仍然不动快照）

```sh
bash tools/nx-ssh.sh cmd 'sh /mnt/mmc/eye28-20261127/20-guards/install-guards.sh'
```

期望：`RESULT=GUARDS_INSTALLED_NOT_ARMED`

失败码见 `docs/current/EYE_GUARDS_R4_INSTALL_GUIDE_2026-11-26.md` 第 4 节。
注意这条要求写入器是**原厂**，所以顺序不能反；装过守卫要先 `withdraw-guards.sh`。

### 第 3 步 · 只读核验装完的样子

```sh
bash tools/nx-ssh.sh cmd '
B=/opt/usr/nx-ks/busybox
$B sha256sum /usr/local/lib/nx-eye-af/*.sh /usr/local/lib/nx-eye-af/*.so /usr/local/lib/nx-eye-af/claim.arm
cat /opt/usr/nx-eye-prepare-*/status
cat /opt/usr/nx-eye-af/capture-writers-before/status
systemctl show di-camera-app.service -p ExecStart
systemctl --all show hib_post.service -p Wants
'
```

| 项 | 期望 |
|---|---|
| 库 | `0955e5d9…` |
| prepare status | `phase=PREPARED_NOT_ARMED` |
| guards status | `phase=GUARDS_INSTALLED_NOT_ARMED` |
| ExecStart | `/bin/sh /usr/local/lib/nx-eye-af/launch.sh` |
| Wants | 含 `nx-eye-resume.service` |

**到这里为止都没有写入 p9，也没有 arm。相机行为与装之前完全一致**（`launch.sh` 的 `eligible()`
要求 `armed` 存在，没有它就走 `exec "$app" "$@"` 原厂路径）。

---

## 这套还没做的事

| 没做 | 为什么 |
|---|---|
| `arm` + `rebuild` | 快照一旦重写就只能整块回滚，且 `bootmode 1\|3` 是否真的经过 `ExecStart` 还没实测 |
| 守卫升 r5 | r5 要接受 `launch.sh` 形态的 App，只有 rebuild 时才用得上，现在改等于引入未验证改动 |
| 与 metol 门禁合并 | 方案在 `docs/current/EYE_VLOG_METOL_COEXIST_2026-11-27.md`，T1 未验完 |

回滚：`sh 20-guards/withdraw-guards.sh` → `sh 10-prepare/prepare.sh rollback <backup>`。

---

## 校验整包

```sh
cd /e/NXWA/_deliverables/eye28-20261127 && sha256sum -c manifest.sha256
```

（WSL 里跑，Windows 的 `sha256sum` 由 Git Bash 提供，路径要注意。）
