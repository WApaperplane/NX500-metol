# 5GHz 探测：缺表策略结论与实验脚本审查

## 补充审查：仅 dry-run 通过，真实探测未通过

本轮审查读取了相机当前 `/usr/bin/wlan.sh`，并在修正本地脚本 dry-run 后通过 SSH stdin 运行 `sh -s -- band --dry-run`，没有向相机上传脚本文件。

原脚本 dry-run 在主分支前会 mkdir、sync，而且 say 会写日志；已修正为 dry-run 不执行这些操作，并打印从普通文件读取的真实 REG/MAC 与完整加载参数。原件备份为 `probe-5ghz.sh.before-dryrun-review.bak`。

`DRY-RUN-RESULT.json` 保存真实输出与前后快照：
- 相机 `/bin/sh` 语法检查、dry-run 均返回0，stderr为空。
- 三个模块的大小、引用计数、加载地址前后一致。
- connmand/net-config/wpa_supplicant 的 PID 和 S 状态前后一致，没有被 STOP。
- `/mnt/mmc/5gprobe` 在前后都不存在。
- wlan.sh、地区文件、功率配置文件 SHA256 前后一致。
- 主要加载参数与原厂常规 start 路径一致，额外参数为 `dev_cap_mask=0xffffffff`；当前无 `/sdcard/wifi-test.tg`，不涉及认证模式附加参数。

以下问题阻断非 dry-run 路径，原文中关于自动恢复、唯一可能发射等断言不能作为安全保证：

1. **恢复不卸载探测模块。** `restore_all()` 直接调 wlan.sh start；原厂 __start 在 cfg80211 仍加载时跳过三个 insmod。成功 load_probe 后走此恢复，会保留探测驱动；仅检查 mlan0 存在会假报恢复成功。恢复必须验证默认能力已重建，而不是只验证接口存在。
2. **隔离失败可能被漏过。** freeze_procs 仅将 STOP 成功的 PID 加入校验；STOP 失败或找不到进程时可能仍返回成功。也没有证明所有自动网络管理与监控路径都受控。
3. **看门狗与主流程并发操作模块。** 超时 restore_all 与尚在运行的主流程没有互斥、接管或终止协议；可能互相卸装。没有全局单实例锁；固定 .done 文件可被另一轮干扰。
4. **手工 restore 不知道冻结了谁。** 新进程的 RESUME_PIDS 为空，无法恢复原探测进程冻结的服务；需要持久化本轮 PID/进程身份和恢复状态，防止 PID 复用。SSH 断开后脚本/看门狗是否存活也未验证，不能依赖普通后台 & 作保证。
5. **探测退出码会被吞掉。** probe_band/probe_pwrtable/probe_scan 的失败可被后续 sync/say 成功掩盖；主流程又无条件 rc=0。load_probe 尾部设置失败也被最终 return 0 掩盖。
6. **scan 实际扫所有允许信道。** `iwlist mlan0 scan` 没有5GHz信道约束，不符合“一次仅5GHz扫描”的实验口径。
7. **txpowercfg 不等于5GHz功率限制全表。** README_MLAN:2430起以及 mapp/mlanutl/mlanutl.c:5804起显示它查询的是按速率格式/带宽/速率索引组织的功率配置。输出没有足以单独确认5GHz各信道及子带限制、校准有效性的证据，不能承诺靠它闭合缺表策略。
8. **缺表处理的表述需收窄。** hostcmd解析器忽略段名已由源码确认；该路径不进行按频段完整性校验，不等于主机软件结构上永远不能做校验，也不说明固件缺表时的默认行为。COUNTRY_POWER_TABLE 的 ENOENT 容忍不能外推至 TXPWRLIMIT_CFG_DATA 分支。

本轮只修了 dry-run 的无副作用性与参数可核对性，没有修复或执行真实探测流程。应完成恢复/隔离/并发模型重写及模拟失败验证后，再重新评估运行风险和请求明确上机决定。

---

2026-11-20 21:2x；本轮**未上机**：未执行 `wlan.sh stop/start`，未改 `dev_cap_mask`、
`reg_alpha2`、`regioncode`，未扫描，未卸载任何模块。仅读代码、二进制与只读状态。

---

## 一、缺表策略：主机侧已闭环，固件侧仍未闭合（但有了可测手段）

### 主机侧（结论确定）

`woal_process_hostcmd_cfg()`（`mlinux/moal_main.c:1688`）的实际行为：

1. **完全忽略段落名**。它只找 `=` 和 `{`（`intf_s` / `intf_e`），
   然后把 `{ … }` 里的十六进制字面量逐字节收进缓冲区 —— 段落叫
   `txpwrlimit_5g_cfg_set_sub0` 还是 `foo`，对它没有任何区别。
2. 遇到 `}` 就把这段当成**一条不透明 hostcmd**（`MRVL_CMDhostcmd` 前缀 + 长度字段），
   经 `woal_priv_hostcmd()` 发给固件。
3. 因此**缺段 = 少发一条命令**。主机侧既没有"完整性校验"，也没有"缺表报错"分支。
   唯一会失败的是 `request_firmware()` 拿不到整个文件。

所以此前那句"主机侧未拒绝"可以升级为更硬的表述：**主机侧在结构上不可能知道、
也不可能校验某个频段的表是否缺失。**

同一份代码里还有一个厂商自己的先例：`COUNTRY_POWER_TABLE` 分支对 `-ENOENT`
显式 `return MLAN_STATUS_SUCCESS`，注释写 "File does not exist, skip download"。
即**整份表缺失时厂商的选择是"跳过"，不是"拒绝启动"**。

### 固件侧（仍未闭合）

固件收到「只覆盖 2.4G 的功率限制」会沿用自身内建默认值，还是会别的方式处理，
**无法从主机源码推出**。要闭合它只有两条路：

- 拿到 5GHz 可用后，用 `mlanutl mlan0 txpowercfg` 读回固件实际生效值（SDIO 命令，
  不发射频），与 `txpwrlimit_cfg.bin` 的 2.4G 段对照；
- 逆向 `sd8887_uapsta.bin` 中处理该 hostcmd 的代码。

脚本已把第一条作为独立阶段 `pwrtable`，与 `band`（注册状态）、`scan`（唯一可能
发射）分开，可以只跑到前两阶段就停。

### 附带确认

`sd8xxx.ko` 的 `.modinfo` 里有 `parm=dev_cap_mask:Device capability mask`
（偏移 313969），**load-time 传参合法**；`perm=0` 只影响 sysfs 可见性，不影响 insmod。

---

## 二、脚本对审查意见的落点（**已被第六节取代**）

脚本：`_deliverables/wifi5g/probe-5ghz.sh`（`bash -n` 通过，未上机运行）

| 审查意见 | 脚本里的处理 |
|---|---|
| A 不保证零发射；三服务可能在接口恢复后自动扫描/重连 | 卸载前 `SIGSTOP` 冻结 `connmand` / `net-config` / `wpa_supplicant`，并逐个校验 `/proc/<pid>/stat` 的 state 为 `T`；失败则不卸载直接中止 |
| 为何用 SIGSTOP 而非 kill | 实读 `net-config` 二进制含 `LoadDriver`、`RemoveDriver`、`__netconfig_wifi_try_to_remove_driver_and_supplicant`、`netconfig_wifi_recover_firmware`；系统另有 `pmon` 与 `daemon-manager`（cgroup `daemon_mgr`，release_agent 指向 `/usr/bin/daemon-manager-release-agent`）。被杀会被拉起，冻结不会 |
| `wlan.sh stop` 会卸三个模块，只装 sd8xxx 不够 | 装载按 `cfg80211 → mlan → sd8xxx` 顺序，三件齐全；卸载后逐个 `lsmod` 复查，模块仍在即判失败 |
| 不能凭 `wlan.sh` 返回码断言成功 | 每一步都复查事实：`lsmod` 是否还有模块、`/sys/class/net/mlan0` 是否出现（50×0.1s 轮询）、进程 state |
| HT40 只是允许，不等于协商 40MHz | 脚本不改带宽；`__start` 尾部命令逐字保留只为让接口可用，不作为吞吐结论依据 |
| 扫描 ≠ 完整验证射频通路 | `scan` 是**独立阶段**，与 `band` / `pwrtable` 拆开；脚本不给出"5G 可用"的结论，只落盘原始输出 |
| loader 只读不足以保证"任何故障都恢复" | 不承诺。恢复失败即写 `NEED_REBOOT` 标记并以 rc=3 退出，明确要求断电 |

---

## 三、失败矩阵（**已被第六节取代**）

| 失败点 | 检测方式 | 处理 | 停机后能否远程救 |
|---|---|---|---|
| 冻结失败 | `state != T` | 中止，不卸载 | 不需要，WiFi 未动 |
| `rmmod` 被拒（refcount>0） | `lsmod` 复查 | 立即 restore，rc=1 | 能（模块未卸干净，接口仍在） |
| `insmod` 返回非 0 | `$?` | 直接 restore | 一般能 |
| `mlan0` 不出现 | 轮询超时 | restore | 一般能 |
| `insmod` 挂死 | 看门狗 240s | restore | **不一定**，可能要断电 |
| 探测阶段进程崩溃 | 看门狗 | restore | 一般能 |
| restore 后 `mlan0` 仍不出现 | 轮询超时 | 写 `NEED_REBOOT`，rc=3 | **要断电** |
| 内核 panic / 整机挂死 | 无 | —— | **要断电**；重启后 `net-config` 会调 `wlan.sh start`，参数为默认值 |

断电恢复的依据：`/var/log/wlanplug_log` 实证启动时 `called by net-config CMD: start`，
且 `/usr/bin/wlan.sh` 位于只读 rootfs（`/dev/root` ext4 ro），boot 参数不可能被污染。
但这只说明"默认值会重新应用"，**不等于任何故障状态下重启必定成功**。

---

## 四、仍未闭合

1. 固件缺 5G 功率段时的实际策略（见上）。
2. `WlanCalData_ext.conf`（1720 B）覆盖哪些频段 —— 它是 Marvell 文本格式的
   hostcmd 流，非二进制；未逐条解码，不能据此判断 5G 校准是否存在。
3. 板级 RF：FCC（A3LNX500）只列 2402–2480 / 2412–2462 MHz，但认证范围
   不等于前端器件的物理构成。
4. "解锁带来数量级收益"仍是**假设**，无任何实测支撑 —— 脚本不承诺收益。

---

## 五、建议的推进顺序（**已被第六节取代**）

1. 先把脚本上机只跑 `band --dry-run`，确认打印出的参数与 `wlan.sh` 逐字一致（不改状态）。
2. 再跑 `band`：若有 5G 信道注册，才继续；若没有，5G 这条线到此为止，回到
   "少传字节"（1024px 预览 8.8×）这条已验证的路径。
3. `pwrtable` 只在 `band` 出现 5G 之后才有意义。
4. `scan` 单独决策，不与前两步合并。

---

## 六、本轮修正（对 8 条审查意见，取代二 / 三 / 五）

脚本已重写：`probe-5ghz.sh`。**`band` / `pwrtable` 仍未执行**；`dry-run` 已在真机
重新验证一次，确认 `/mnt/mmc/5gprobe` 未创建、三模块仍为 3、三服务仍为 `S`。

| 审查条目 | 修正 | 残余局限 |
|---|---|---|
| 1 恢复不卸载探测模块 → 假报成功 | `restore_all` 改为 rmmod 三件 → 复查 `lsmod` 为空 → 才调 `wlan.sh start`；再加功能校验 | 无（等价于原厂 start 前置条件） |
| 2 隔离失败被漏过 | 任何 `SIGSTOP` 失败立即返回失败；卸载前 `verify_frozen` 重新枚举一次 | **未证明**所有自动网络管理与监控路径都受控，只覆盖已知三个服务 |
| 3 看门狗并发 / 无单实例锁 / `.done` 可被干扰 | 加 `RUNLOCK`（单实例）与 `MLOCK`（模块临界区）；看门狗仅心跳停滞才接管且须先拿 `MLOCK`；`DONE` 写运行令牌 | SSH 断开后脚本/看门狗是否存活**未验证** |
| 4 手工 restore 不知冻结了谁 / PID 复用 | `freeze.pids` 记录 `pid name starttime`；解冻前校验 `comm` 与 `starttime`，不符则拒绝 `CONTINUE` | 依赖 `/proc/<pid>/stat` 字段序，comm 含空格时不可靠（本机三名均无空格） |
| 5 探测退出码被吞 | `probe_band` / `probe_pwrtable` 各自 `return` 真实结果；`load_probe_chain` 中 `ifconfig up` 失败即返回非 0；主流程按 `probe_rc` 出 4 | `htcapinfo`/`httxcfg`/`power off` 失败只记日志，不影响只读探测 |
| 6 `iwlist scan` 扫全部信道 | **移除 scan 阶段**。真机无 `iw`，`iwlist` 不能按信道约束 | 5GHz-only 扫描在本工具集下做不到，除非自建工具 |
| 7 `txpowercfg` ≠ 5G 全表 | 改用 `mlanutl mlan0 hostcmd <conf> txpwrlimit_cfg_get`，把 conf 里那块原样下发；真机已确认 `hostcmd` 子命令存在 | 只反映固件当前持有的限制表，**不证明**各信道限值/校准有效 |
| 8 缺表表述过宽 | 收窄为："该 hostcmd 路径不做按频段完整性校验" | 固件缺段时的默认行为仍未闭合；`COUNTRY_POWER_TABLE` 的 ENOENT 容忍不外推 |

### 自己发现并修掉的一个同类 bug

`iwlist mlan0 freq` 的输出是 `Channel 01 : 2.412 GHz`，不是 `2412 MHz`。
早先的恢复校验写的是 `[45][0-9]{3} MHz`，**永远不命中**，等于无论如何都报恢复成功
—— 与审查第 1 条同性质的假报。已改为 `(^|[^0-9])[45]\.[0-9]{3} GHz`，
并用真机输出与合成 5GHz 输出双向验证：当前只命中 2.4G 时不报错，注入
`Channel 36 : 5.180 GHz` 时正确命中。

### 修正后的失败矩阵（替换第三节）

| 失败点 | 检测 | 处理 | 能否远程救 |
|---|---|---|---|
| `SIGSTOP` 失败 | 失败即返回 | 中止，不卸载 | 不需要，WiFi 未动 |
| `rmmod` 后模块仍在 | `lsmod` 复查 | 判恢复失败，`NEED_REBOOT` | 否，需断电 |
| `insmod` 返回非 0 | `$?` | 走 restore | 一般能 |
| `mlan0` 不出现 | 50×0.1s 轮询 | 走 restore | 一般能 |
| `ifconfig up` 失败 | `$?` | 跳过探测，走 restore | 一般能 |
| `insmod` 挂死 | 心跳停滞 90s | 看门狗接管（须先拿锁） | **不一定** |
| 主流程异常退出 | 心跳停滞 | 看门狗接管 | 一般能 |
| restore 后仍有 ≥4.9GHz 信道 | 信道表判据 | 判恢复失败，`NEED_REBOOT` | 否，需断电 |
| restore 后读不到信道表 | 判据无 `channels in total` | 判失败，不假报成功 | 否 |
| 内核 panic / 挂死 | 无 | —— | **要断电** |

### 推进建议（替换第五节）

1. `status` 先看现场：确认无 `NEED_REBOOT` / 无残留锁 / 模块为默认三件。
2. `band --dry-run` 已通过，可跳过；若要再确认可重跑（无副作用）。
3. **`band` 是第一个需要你明确点头的动作**：会冻结三个服务、卸载三个模块，
   WiFi 中断约数十秒，失败则需断电。
4. `pwrtable` 只在 `band` 出现 ≥4.9GHz 信道后才有意义。
5. `scan` 已从脚本移除；若 `band` 通过，再单独讨论用什么工具做 5GHz-only 扫描。

---

## 七、21:4x：功率表用只读 GET 问到了，且不需要解锁

### 触发点
审查第 3 条指出：通用 hostcmd 转发路径未见必须先启用 `BAND_A` 的判断。核对源码成立——
`woal_priv_hostcmd`（`moal_eth_ioctl.c:315`）只校验长度就转发 `MLAN_OID_MISC_HOST_CMD`。
于是不必先卸载解锁，可以在**默认驱动**上直接发只读 GET。

### 编码（上一轮失败的根因）
`mlanutl` 要的是结构化 conf：`CmdCode=0x00fb` + `Action:2=0` + `SubBand:2=0x10`，
与驱动侧 `txpwrlimit_cfg.bin` 里的原始十六进制块**不是同一种格式**。
厂商参考 `mapp/mlanconfig/config/txpwrlimit_cfg.conf` 就给了这种写法，字段宽度相加
正好等于原始块 payload 长度（4 字节），双向对得上。

### 真机结果（零风险：未冻结、未卸载、未改参数、未扫描）
五组全部 `Result=0000`：2.4G 14 条(1–14)；0x10 八条(36–64)；0x11 十二条(100–144)；
0x12 **仅一条(ch144，异常)**；0x13 十四条(183–196 + 7,8,11,12,16,34)。
条目结构 8B 头 + N×(rate,power)，2.4G 28B/条（10 档）、5G 38B/条（15 档），与 Size 精确吻合。

### 这意味着什么
`txpwrlimit_cfg.bin` 只是**主机侧覆盖文件**，缺 5G 段不等于固件没有 5G 表。
此前"5G 功率表没随机型发布"的判断需要撤回：固件明确持有 5G 每信道每速率限值。
"四层一致裁剪"现在只剩两层：驱动 `dev_cap_mask` 与 FCC 只认证 2.4G。

### 脚本对应改动
- 新增 `getpw` 阶段：**零风险**，写 `/tmp` 的 conf，逐组检查退出码与 `Result=0000`，
  结果落 `pwrtable.txt`。已在真机执行，退出码 0。
- `pwrtable` 阶段作废。
- 第 1 条阻断：主流程现在**自己持 MLOCK**（`mlock_take` 在卸载前），不再只有恢复函数持锁。
- 第 4 条：`DONE` 比对运行令牌，不再只 `test -f`。
- 第 5 条：`thaw_procs` 有未解冻项时**保留** `freeze.pids`，不再一边失败一边删证据。

### 仍然开放
- 这些限值是板级校准产物还是通用默认表，未证明。
- sub2 只有 ch144，与预期 149–165 不符，原因未明。
- 板级 RF 前端是否存在，仍未验证——这才是 `band` 要回答的问题。
