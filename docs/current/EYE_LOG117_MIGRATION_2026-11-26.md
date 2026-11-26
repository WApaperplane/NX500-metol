# Eye AF / Log117 迁移开发记录

## 固定输入

- Log117：NoireSchwarz/NX500_SRW_reprocessing_6.5K-oversampling，`eafc274e0ef0b6cb95b5ad958b2f1ac86dfc3562`。
- Unified R1 ZIP SHA-256：`49de7be7e19899526556178e386b5beafb43c2049f43ebf51b7b41e33e5fe766`。
- YuNet：WApaperplane/NX500-metol 的 `nxwa`，`4a64b061d41da0701f303646cea096daf61c0b93`。`master` 无此贡献目录。
- 用户报告：神经网络方案指令数约减半，但暗光拉风箱。尚无本轮独立性能或暗光合焦实测，不能据此修改镜头控制参数。

## Log117 载荷构建

`tools/log117/build_bundle.py ARCHIVE NEW_OUTPUT` 校验完整 ZIP 身份，沿原脚本 `$D/` 引用生成保守依赖闭包，额外保留原始许可证、来源和 Flat117 数据。29 项文件逐一核对原发布清单；脚本的内部 hash 绑定保持原样。输出 `payload.sha256` 与统一框架的 `manifest.json`。

此闭包包含诊断分支需要的菜单二进制，但不运行上游菜单、keyscan 或安装器，不包含替换原厂核心库的载荷。闭包验证属于静态引用及文件身份验证；机身原厂共享库、硬件事务和 LUT 消费仍需真机核验。

```sh
python3 tools/log117/build_bundle.py /path/to/NX500_Full_Mod_EyeAF_0.4.0.zip /path/to/new-bundle
```

`scripts/gamma-install.sh log117 [install|check]` 分派至独立的 `gamma-log117-install.sh`。安装器将 `payload.sha256` 的固定 hash 作为可信根，核验所有文件及父目录链接，复制到同分区临时目录后再核验；保留旧目录和 `state`。拒绝错误固件、活动的 gamma/LUT 会话及并发安装。`check` 只验证文件，不执行 ARM 引擎。

机身准备阶段的显式调用形式如下；这不是已进行的部署，也不是整包安装流程：

```sh
GAMMA_SRC=/mnt/mmc/PATH_TO_VERIFIED_BUNDLE sh /opt/usr/nx-ks/gamma-install.sh log117 install
sh /opt/usr/nx-ks/gamma-install.sh log117 check
```

返回 `INSTALLED` / `PAYLOAD_VERIFIED` 仅说明文件状态；`ACTIVATED=false`。安装不执行曲线操作，不触发快启重建。现阶段尚未完成 metol LUT/watcher 与 Log117 内部 LUT 引擎的资源所有权联调，不能将该载荷当作已通过联合验收的录像版本。

主机验证：WSL Python 3，真实 shell 入口，临时假根；覆盖旧状态保留、完整载荷验证、活动会话、源文件损坏、不支持固件。另有 vlog 守卫错误与视频恢复分支回归。没有以模拟测试替代真机验收。

## 启动和验收边界

YuNet 导入目录保留原始源码、模型来源和许可证。原厂仍控制镜头；模型眼点抖动、检测延迟、低对比度下的镜头搜索应分开测量。暗光验收至少记录曝光/照度、目标眼框稳定性、检测失效回退及实际合焦耗时，不能仅以指令数作合焦速度结论。

Eye 必须在制作本机快照时随原相机 App 加载；恢复令牌只启用已在同一进程中的库。蓝牙后置脚本不能替代预加载。不得通过重启正在运行的 di-camera-app 验证安装。

统一框架目前的 PC stage 是载荷准备，不发布相机触发入口。相机端事务、制作前服务图、快照备份/写入、恢复代次和同次启动眼框+metol按键验收仍需逐项验证。任何 PC 回执都不能被显示为相机安装成功。

## 本轮产物与最终主机验证

- Log117 载荷：`E:/NXWA/_deliverables/log117-payload-r3-20261126`，29 项原发布文件及校验表，manifest 共30条。
- YuNet 载荷：`E:/NXWA/_deliverables/eye-yunet32-payload-20261126`，由 `contrib/nx500-eye-af/build_metol_bundle.py` 从实际ARM构建结果生成。统一框架允许 `eye-yunet32` 固定目标 `/usr/local/lib/nx-eye-af`。
- YuNet ARM库 SHA256：`0955e5d90a61d33740c4fa18150e7499db4e959a536cd4c84a1eb04b3e587cbb`。构建命令与回放原始输出在 `contrib/nx500-eye-af/out/verification.log`，包括57个ARM场景、90稳定回调和scalar/NEON回放。
- 新增机身适配源代码在 `contrib/nx500-eye-af/metol/`：制作阶段launcher、排他claim生成器、恢复门禁、service/drop-in示例；不启动第二个keyscan。实际service挂接、快照备份及OEM写入事务尚未实现。
- 包生成器生成固定哈希 verifier；单项篡改测试覆盖9项载荷。统一框架分别以真实CLI核验两个生成包。
- 主机测试：统一安装框架6项、Eye适配5项、Log暂存到假根安装联通1项、既有vlog回归2项，合计14项通过。没有相机硬件参与。

Windows `python` 为占位，`py -3` 实测无可用运行时，故测试在WSL Python3运行；真实Windows SD卷序列号API尚未实测。Eye构建依赖独立WSL venv内ziglang 0.13.0、ONNX 1.17、NumPy、Unicorn及pyelftools。

继续所需机身事实：当前已安装版本、实际相机service/drop-in/hib_post依赖、原厂库身份、本机p9几何与可用备份、当前连接方式。没有这些输入，不能把示例service配置变成会写快照的安装入口。暗光拉风箱尚未定位或修复。

## 机身上线核验（192.168.0.104）

SSH确认NX500 1.12、ARMv7，boot_id `dbb7e6ce-8b5e-4d2a-ba3e-bf5ea16d54bc`，bootmode=2、USB=0、无活动swap。相机PID247，单一keyscan PID343，未见可写MP4；已有RC5 gamma引擎hash `5edf35e0...`。Eye及上游合并快启入口均未安装。

App、libcapture-fw-prod和libface与上游原厂基线一致。实际 `/usr/lib/systemd/system/di-camera-app.service` 包含 `ExecStartPre=-/opt/usr/nx-ks/dicam/t2_premount.sh`，并非完全原厂service；当前快照恢复中实际App hash为55dcc781...，而p14候选c2 hash为ec395123...。制作新快照前必须审计T2前置挂载是否会令新App载荷与核验基线不同，不能直接覆盖service或宣称已证明兼容。

`hib_post.service` 当前无Wants；没有drop-in或新增恢复入口。原厂hibernation.sh会写p9镜像头，make_snapshot.sh会先清除80MiB并强制重启：本轮没有执行它们。

快照已通过SSH读取至 `E:/NXWA/rea-evidence/20261126-eye-log117/mmcblk0p9-before.img`，大小157285888（307199扇区）。机身读出前后MD5及本地MD5均为`8a669a6a1b503d541a7f4a463d49ca5e`，本地SHA256为`46e69b6fc342885147a88831e2723ff87c282326ab457a80bbdd35b5742a3f5e`。这是一份读取一致的备份，未做写回恢复测试。启动配置、旧Gamma和dicam另存startup-before.tar，SHA256为`d376a1567f4a39706534ffc669b95c63e1681e980cd16ada0e20681bec67f26d`。原件均保留。

实机发现裸sha256sum缺失，BusyBox applet可用。已修正安装器hash回退、Log私有compat/sha256sum及vlog子进程PATH、Eye生成校验器。新Log payload.sha256可信hash为`a30ad550a68653d475aee8201e20dfbd16fd91802f2f5a41ea2e49c60fdcbf6f`；旧r3载荷由r4取代，勿用新安装器安装旧r3。

本轮当前载荷为 `_deliverables/log117-payload-camera-r4-20261126`（29项上游文件+兼容shim+checksum表）及 `_deliverables/eye-yunet32-camera-r2-20261126`。已仅暂存至机身 `/opt/usr/nx-ks/extensions/stage-20261126-eye-log117/{log117,eye}`；安装器check和Eye verifier在机身均退出0，Log ARM `--version`返回Gray117。未执行gamma状态更改、服务重启、预加载或快照写入。证据在同一rea-evidence目录camera-payload-check.txt与camera-version-backup-confirm.txt。

剩余开发：T2共存方案、service图检查及挂接、原厂制作阶段双重写入守卫、完整回退事务、Log与metol LUT/watcher联合所有权；剩余实测：眼框与菜单同次恢复、曲线/LUT实际恢复、录像和暗光对焦。文件校验与版本输出不能代替这些验收。

## Eye 准备入口已安装（续）

已完成并安装准备阶段入口，状态 `PREPARED_NOT_ARMED`。原service文件hash仍ac1bab...，通过 `/etc/systemd/system/di-camera-app.service.d/70-metol-eye.conf` 选用root分区launcher，保留既有T2 ExecStartPre；`hib_post.service` 的Wants已实际显示nx-eye-resume.service。当前App仍为PID247，未重启。安装备份目录 `/opt/usr/nx-eye-prepare-20150102082526-1118`。机身时钟为历史时间，记录引用使用目录标识，不把它视作真实日期。

准备安装器由 `tools/extensions/build_eye_prepare.py` 生成，独立pin验证器及当前service/T2/c2身份，复制后再次验证；服务图失败会自动撤回新文件，成功后支持显式rollback并保存撤下文件。主机成功/失败回退两项通过，当前使用eye-prepare-r4生成脚本（机身存放仍在eye-prepare-r3暂存目录）。launcher仅允许原厂App或当前精确T2 hash接收预加载，未知程序仍原样启动。

已从原备份派生并安装保护后的hibernation.sh、make_snapshot.sh和prewrite服务入口；备份在 `/opt/usr/nx-eye-af/capture-writers-before`，状态 `GUARDS_INSTALLED_NOT_ARMED`。前者在原厂首个镜像头写入前再核验捕获manager，后者阻止原厂独立维护命令先清80MiB。源码来自固定上游eafc274的capture-check并改为metol Eye恢复服务。主机守卫条件及保护安装/失败回退共3项通过；实机调用record在bootmode2明确STOP/NOT_A_CAPTURE_BOOT，未进入写入。

显式触发脚本 `/usr/local/lib/nx-eye-af/arm-rescue.sh` 已就位但尚未调用。它要求操作员确认、重新核验备份对应的完整p9 MD5、原厂写入保护hash、实际启动依赖及无开放MP4后，才创建generation并请求原厂rescue；不直接擦写p9。rescue拒绝且尚未进入制作时会归档本轮armed。

回退顺序：仅在未armed且未claim时先withdraw-guards.sh还原OEM writer并撤guard入口，再prepare.sh rollback对应备份目录；装有writer guard时，prepare rollback会拒绝删除其依赖运行目录。涉及已制作快照时须另行读取该代记录，不能将准备阶段回退冒充快照恢复。

用户已表示可现场配合。尚需实际确认English、M、满电、卸镜头及机身盖、熄屏/省电设置，完成后才安排断开无线与原厂rescue制作。电池sysfs capacity=1460、status=Unknown，没有百分比依据，不能套用“>=95即满电”。目前无armed/launch-claim，p9仍为原MD5；完整制作、复原后的眼框+metol按键和暗光验收尚未通过。Log切换尚未执行。

## 按 R1.1 重做写入守卫（r3，PC 回放通过）

上一版守卫把 `--at-image-write || exit 1` 放在原厂首个镜像头写入之前。拒绝时 hibernation.sh 直接非 0 退出：zram 已 swapon、卡灯已置 timer、`/tmp/.hib_ready` 从未发布 —— 原厂相机等这个标记，屏幕就一直黑着。这正是软砖的可复现路径，R1.1 的 AUDIT 记录了同一失败条件。

改为 R1.1 的两段门禁（`tools/extensions/build_eye_capture.py::protected_header`）：

| 位置 | 拒绝时的行为 |
|---|---|
| `echo "Starting hibernation ... "` 之后、zram 之前（`--at-preparation`） | `touch /tmp/.hib_ready`；`exit 0` —— 原厂相机照常启动 |
| 首个镜像头 `dd` 之前（`--at-image-write`） | `swapoff /dev/zram0`（command -v → /sbin → busybox 三路回退）、`touch /tmp/.hib_ready`、`exit 0` |

卡灯 `echo timer > /sys/class/leds/card/trigger` 从原位置删除，移到写门禁**之后**：拒绝时不再假装有进度。镜像头因此在两个拒绝点都保持原样。

`verify-before-capture.sh` 增加 `--at-preparation` 阶段（`stage=image-preparation`），复用 `image-header` 的写入器校验分支；`writer_reason` 判据由 `!= image-header` 改为 `= before-capture`，两种写入期阶段都按「不是当前原厂主进程」报错。

其它两处：

- `arm-rescue.sh` 的哈希不再硬编码，由 `build_eye_capture.py` 在生成时把 `@GUARD@/@HEADER@/@FACTORY@` 填成同包内实际文件的 SHA-256（旧版写死 `0299830…`/`1e2bcf8…`，守卫一重建就会失配）。
- rescue 被拒时同时撤下 `state/armed` 与 `state/capture-origin`（原来只撤 armed），否则下次启动只剩半套代次、重试会被 `ALREADY_ARMED` 挡住。
- 电量判据移入 `battery-gate.sh`：`capacity` 只有在 0..100 时才当百分比（<95 拒绝）；`1460` 这类原始值不再当作百分比通过，也不静默放过，而是要求操作员显式加 `--battery-confirmed-full`。读不到同样要求该声明。判定结果写入 `state/battery.txt`。

PC 回放（`test_server/extensions/test_hibernation_handoff.py`，用 `startup-before.tar` 里的真实原厂 hibernation.sh，sha256 `dd3687d3…`）：

| 情形 | 断言 | 结果 |
|---|---|---|
| 准备阶段拒绝 | 退出码 0、`.hib_ready` 已发布、p9 未创建、卡灯为空、无 mkswap/swapon | 通过（WSL Python 3） |
| 写镜像头阶段拒绝 | 退出码 0、`.hib_ready` 已发布、p9 未创建、卡灯为空、swapon 之后有 swapoff | 通过 |
| 全程通过 | 退出码 0、p9 写入 512 字节、卡灯 timer、两次门禁按序 | 通过 |
| 守卫 `--at-preparation` | 当前写入器 PASS；外部写入器/过期 manager STOP | 通过 |
| battery-gate | 1460 无声明 = 3、有声明 = 0；42 = 4；95 = 0 | 通过 |

产物：`_deliverables/eye-capture-guards-r3-20261126`（arm-rescue / battery-gate / hibernation / make_snapshot / verify-before-capture / capture-profile，`sha256sum -c capture.sha256` 全 OK），`install-guards.sh` 同步安装 arm-rescue.sh 与 battery-gate.sh。

机身待办：现装守卫仍是旧版（guard `0299830…`、hibernation `1e2bcf8…`），r3 必须先 `withdraw-guards.sh` 再重装；两段门禁的真实 systemd 关系、`--at-preparation` 在真实 hibernation.service 下 PPID 是否等于 ExecMainPID、以及 `command -v swapoff` 在机身 PATH 下的实际命中分支，都还没有实机证据。回放证明的是脚本形状与失败交接，不是机身行为。

