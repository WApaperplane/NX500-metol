# Eye-AF 写入守卫：r2 → r4 上机教程

适用：NX500 固件 1.12 + NX-KS2，机身已装 r2 守卫（`hibernation.sh` = `1e2bcf83…`），
尚未 arm、未做过 snapshot。

本教程只做三件事：**撤下 r2 → 装上 r4 → 只读核验**。不写入 p9、不重启 App、
不 arm、不触发任何 capture。

---

## 0. 先看清楚改了什么

| 文件 | 原厂 / 旧 | r4 | 谁写 |
|---|---|---|---|
| `/usr/bin/hibernation.sh` | `dd3687d3…`（原厂）→ `1e2bcf83…`（r2） | `3779b8a1…` | install |
| `/usr/bin/make_snapshot.sh` | `08df6e37…`（原厂） | `ea12c6cf…` | install |
| `/usr/local/lib/nx-eye-af/verify-before-capture.sh` | 无 | `0c7cfb6a…` | install |
| `/usr/local/lib/nx-eye-af/arm-rescue.sh` | 无 | `a73b209a…` | install |
| `/usr/local/lib/nx-eye-af/battery-gate.sh` | 无 | `b00838c5…` | install |
| `/etc/systemd/system/hibernation.service.d/75-nx-eye-prewrite.conf` | 无 | 3 行 drop-in | install |
| `/opt/usr/nx-eye-af/capture-writers-before/` | 无 | 原厂备份 + `status` | install |

r4 相对 r2 的实质差别：拒绝时**不再非 0 退出**，而是 `swapoff zram` +
`touch /tmp/.hib_ready` + `exit 0`，把启动交回原厂相机。r2 是黑屏软砖的直接原因。

---

## 1. 前置检查（只读，必须全绿）

```sh
export NXHOST=192.168.0.xxx        # 以实际 IP 为准
cd /e/NXWA/nx500-modding-work
bash tools/nx-ssh.sh cmd '
B=/opt/usr/nx-ks/busybox
echo "-- 1 prepare --";   ls -d /opt/usr/nx-eye-af /usr/local/lib/nx-eye-af
echo "-- 2 payload --";   /bin/sh /usr/local/lib/nx-eye-af/verify-payload.sh /usr/local/lib/nx-eye-af; echo rc=$?
echo "-- 3 writers --";   $B sha256sum /usr/bin/hibernation.sh /usr/bin/make_snapshot.sh
echo "-- 4 not armed --"; ls /opt/usr/nx-eye-af/armed /opt/usr/nx-eye-af/launch-claim 2>&1
echo "-- 5 status --";    cat /opt/usr/nx-eye-af/capture-writers-before/status 2>&1
echo "-- 6 wake --";      systemctl --all show hib_post.service -p Wants
echo "-- 7 root --";      mount | grep " / "
'
```

判据：

| 项 | 必须 | 不符时的含义 |
|---|---|---|
| 1 | 两个目录都存在 | 缺 ⇒ 刷机把 `/opt/usr` 清了，先重装 prepare r5，**停在这里** |
| 2 | `rc=0` | 非 0 ⇒ 载荷被改过，先查是谁改的 |
| 3 | `hibernation.sh` = `1e2bcf83…`，`make_snapshot.sh` = `ea12c6cf…` | 两个都是 `dd3687d3…`/`08df6e37…` ⇒ 机身本来就是原厂，**跳过第 3 节**直接装；其它组合 ⇒ 停 |
| 4 | 两条都 `No such file` | 存在 ⇒ 已经 arm 过，先人工复核，**不要继续** |
| 5 | `phase=GUARDS_INSTALLED_NOT_ARMED` | 文件缺失且第 3 项是原厂 ⇒ 正常（从未装成），直接装；文件存在但不是这行 ⇒ 按第 6 节手工恢复 |
| 6 | 含 `nx-eye-resume.service` | 不含 ⇒ `WANTS_NOT_LOADED`，装到最后一刻会全量回滚 |
| 7 | `/dev/root ... ro` | 不是 ro ⇒ 上次改机没恢复，先 `mount -o remount,ro /` |

---

## 2. 推包

```sh
cd /e/NXWA/nx500-modding-work
bash tools/nx-ssh.sh push /e/NXWA/_deliverables/eye-capture-guards-r4-20261126 /mnt/mmc/
bash tools/nx-ssh.sh cmd '
B=/opt/usr/nx-ks/busybox
D=/mnt/mmc/eye-capture-guards-r4-20261126
sync; sleep 2
cd "$D" && chmod 755 *.sh && $B ls -l
$B sha256sum -c capture.sha256
'
```

`sha256sum -c` 必须 7 条全 `OK`。exfat 会在拔插后产生重复目录项，若 `ls | wc -l`
大于 `ls | sort -u | wc -l`，删掉重推。

---

## 3. 撤下 r2 守卫（一条命令）

```sh
bash tools/nx-ssh.sh cmd 'sh /mnt/mmc/eye-capture-guards-r4-20261126/withdraw-guards.sh'
```

期望：

```
RESULT=GUARDS_WITHDRAWN_SNAPSHOT_UNCHANGED
WRITER_WITHDRAWN=r2
```

它做的事：两个写入器恢复成 backup 里的原厂字节；drop-in 移到 backup；
`/usr/local/lib/nx-eye-af/` 下三个脚本移到 backup；`daemon-reload`；
`/dev/root` 回到 ro。**不碰 p9。**

失败对照：

| RESULT | 含义 | 动作 |
|---|---|---|
| `CAPTURE_STATE_REQUIRES_REVIEW` | 存在 `armed`/`launch-claim` | 停。先查是不是上次 capture 没清干净 |
| `BACKUP_MISSING` | `capture-writers-before/` 不在 | 没有原厂备份，改用第 6 节手工恢复 |
| `BACKUP_CHANGED` | backup 里的原厂字节被改了 | 停，说明有人动过备份 |
| `WRITER_CHANGED` | 当前 `hibernation.sh` 既不是 r2 也不是 r4 | 停，先搞清机身是什么版本 |
| `FACTORY_WRITER_CHANGED` | `make_snapshot.sh` 不是 r2/r4 那一对 | 同上 |
| `GUARD_UNKNOWN` | 守卫脚本哈希不认识 | 停，别让 withdraw 删来路不明的文件 |

---

## 4. 装上 r4

```sh
bash tools/nx-ssh.sh cmd 'sh /mnt/mmc/eye-capture-guards-r4-20261126/install-guards.sh'
```

期望：`RESULT=GUARDS_INSTALLED_NOT_ARMED`

它按这个顺序做，任一步失败由 trap 自动回滚（写入器复原、新文件移进 backup、
`daemon-reload`、root 回 ro）：

1. 校验 `capture.sha256` 自身哈希（硬编码在脚本里）→ 再逐条校验 7 个文件
2. 要求 prepare 在、未 arm、载荷未变
3. 要求两个写入器是**原厂**字节
4. 要求守卫/臂助脚本**不存在**、drop 目录**不存在**
5. 备份原厂写入器 → 校验备份 → 开始写
6. 写 5 个文件 + drop-in → `daemon-reload`
7. 校验 `hib_post.service` 的 Wants 含 `nx-eye-resume.service`
8. 写 `phase=GUARDS_INSTALLED_NOT_ARMED` → `sync`

失败对照：

| RESULT | 含义 | 动作 |
|---|---|---|
| `UNTRUSTED_GUARDS` | `capture.sha256` 被改过 | 重推整个目录，别改任何字节 |
| `CORRUPT_GUARDS` | 有文件内容不符 | 同上 |
| `EYE_NOT_PREPARED` | `/opt/usr/nx-eye-af` 不在 | 先装 prepare r5 |
| `EYE_PAYLOAD_CHANGED` | 载荷校验失败 | 先查载荷 |
| `FOREIGN_HEADER_WRITER` | `hibernation.sh` 不是原厂 | **还没 withdraw**，回第 3 步 |
| `GUARD_EXISTS` / `RESCUE_EXISTS` | 旧守卫还在 | 同上 |
| `FOREIGN_CAPTURE_OVERRIDE` | drop 目录已存在 | 有人手建过，先看里面是什么 |
| `BACKUP_EXISTS` | 有 backup 但状态不是 `GUARDS_WITHDRAWN` | 停，先读 `status` |
| `WANTS_NOT_LOADED` | prepare 的单元没挂上 | 重装 prepare，或查 `nx-eye-resume.service` |

---

## 5. 装完立刻只读核验（四条）

```sh
bash tools/nx-ssh.sh cmd '
B=/opt/usr/nx-ks/busybox
echo "-- 1 哈希 --"; $B sha256sum /usr/bin/hibernation.sh /usr/bin/make_snapshot.sh \
  /usr/local/lib/nx-eye-af/verify-before-capture.sh /usr/local/lib/nx-eye-af/arm-rescue.sh \
  /usr/local/lib/nx-eye-af/battery-gate.sh
echo "-- 2 状态 --"; cat /opt/usr/nx-eye-af/capture-writers-before/status
echo "-- 3 备份 --"; $B sha256sum /opt/usr/nx-eye-af/capture-writers-before/hibernation.sh \
  /opt/usr/nx-eye-af/capture-writers-before/make_snapshot.sh
echo "-- 4 生效 --"; systemctl show hibernation.service -p ExecStartPre
echo "-- 5 未 arm --"; ls /opt/usr/nx-eye-af/armed 2>&1
'
```

| 项 | 期望 |
|---|---|
| 1 | `3779b8a1…` / `ea12c6cf…` / `0c7cfb6a…` / `a73b209a…` / `b00838c5…` |
| 2 | `phase=GUARDS_INSTALLED_NOT_ARMED` |
| 3 | `dd3687d3…` / `08df6e37…`（原厂，随时可回滚） |
| 4 | 含 `verify-before-capture.sh --when-capturing` |
| 5 | `No such file` |

五项全对才算装上。**到这一步还没有任何东西被 arm。**

---

## 6. 手工回原厂（withdraw 用不了时）

只在前面的脚本报 `BACKUP_MISSING` / `WRITER_CHANGED` / 安装中途断电时用。
先确认 backup 里的字节是原厂：

```sh
BB=/opt/usr/nx-ks/busybox
$BB sha256sum /opt/usr/nx-eye-af/capture-writers-before/hibernation.sh
# 必须 = dd3687d3231ec32bbdcaaf3aac3015842f4ec703269a5eef71213e3e75447361
```

确认后：

```sh
BB=/opt/usr/nx-ks/busybox
BK=/opt/usr/nx-eye-af/capture-writers-before
mount -o remount,rw /
cp "$BK/hibernation.sh" /usr/bin/hibernation.sh
cp "$BK/make_snapshot.sh" /usr/bin/make_snapshot.sh
chmod 755 /usr/bin/hibernation.sh /usr/bin/make_snapshot.sh
rm -f /usr/bin/hibernation.sh.nx-eye-new /usr/bin/make_snapshot.sh.nx-eye-new
ls /etc/systemd/system/hibernation.service.d/     # 先看，只有 75-nx-eye-prewrite.conf 才删
rm -rf /etc/systemd/system/hibernation.service.d
systemctl daemon-reload
sync
mount -o remount,ro /
$BB sha256sum /usr/bin/hibernation.sh /usr/bin/make_snapshot.sh
```

最后两行应为 `dd3687d3…` 和 `08df6e37…`。`/opt/usr/nx-eye-af` 与
`/usr/local/lib/nx-eye-af` 里的残留文件不参与启动，留着不影响开机。

---

## 7. 现在还不能做的

| 红线 | 原因 |
|---|---|
| 不要 `arm-rescue.sh --operator-ready` | 守卫只是"写之前先问一句"，不保证 p9 内容可用 |
| 不要手动跑 `/usr/bin/hibernation.sh` | 它一跑就开始动 zram 和镜像头 |
| 不要以为装了守卫就安全 | r4 保证的是**拒绝时能开机**，不是**答应时不会坏** |
| 不要在这之后改守卫脚本 | `arm-rescue.sh` 里 pin 的是 `0c7cfb6a…`，改一个字节就 `CAPTURE_GUARD_CHANGED` |

下一步（T2 共存 / 实际制作）另开一轮，先补 p9 内容验证。

---

## 附：一次完整的上机命令序列

```sh
cd /e/NXWA/nx500-modding-work
export NXHOST=192.168.0.xxx

# 1 只读检查（照第 1 节，全绿再往下）
# 2 推包
bash tools/nx-ssh.sh push /e/NXWA/_deliverables/eye-capture-guards-r4-20261126 /mnt/mmc/
bash tools/nx-ssh.sh cmd 'B=/opt/usr/nx-ks/busybox; D=/mnt/mmc/eye-capture-guards-r4-20261126; sync; sleep 2; cd "$D" && chmod 755 *.sh && $B sha256sum -c capture.sha256'

# 3 撤旧
bash tools/nx-ssh.sh cmd 'sh /mnt/mmc/eye-capture-guards-r4-20261126/withdraw-guards.sh'
# 4 装新
bash tools/nx-ssh.sh cmd 'sh /mnt/mmc/eye-capture-guards-r4-20261126/install-guards.sh'
# 5 核验（照第 5 节）
```
