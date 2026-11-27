#!/bin/sh
# =====================================================================
# probe-rebuild.sh —— 快照重建前的只读探针（不写任何文件、不碰 p9）
#
#   目的：在决定重建之前，一次性拿回「重建链路上所有还未知的量」。
#   输出：KEY=value 行，供人读、也供脚本比对。
#
#   背景（源码级，2026-11-27 复核 startup-before.tar）：
#     /usr/bin/make_snapshot.sh  ← 只有它擦 p9；首次运行只建 /etc/snapshot
#     /usr/bin/hibernation.sh    ← 只做 mkswap+swapon+remount ro，然后 touch
#                                   /tmp/.hib_ready；对 1/2/3 无差别对待
#     hib_post.path              ← PathExists=/tmp/.hib_post，恢复后跑 init.sh
#     所以「重建一次」= make_snapshot.sh 跑两遍（第二遍才 dd 掉 80MiB）
#
#   用法（相机端）:
#     sh /opt/usr/nx-ks/probe-rebuild.sh
#   PC 端:
#     NXHOST=<ip> bash tools/nx-ssh.sh push contrib/nx500-eye-af/capture/probe-rebuild.sh \
#       && NXHOST=<ip> bash tools/nx-ssh.sh cmd 'sh /opt/usr/nx-ks/probe-rebuild.sh'
# =====================================================================
BB=/opt/usr/nx-ks/busybox
p() { printf '%s=%s\n' "$1" "$2"; }
c() { if [ -r "$1" ]; then cat "$1" 2>/dev/null; else echo MISSING; fi; }
h() { if [ -f "$1" ]; then "$BB" sha256sum "$1" 2>/dev/null | cut -d' ' -f1; else echo MISSING; fi; }
# 块设备不是普通文件：-f 对 /dev/mmcblk0p9 恒假，曾经因此恒输出 MISSING。
m() { if [ -b "$1" ] || [ -f "$1" ]; then "$BB" md5sum "$1" 2>/dev/null | cut -d' ' -f1; else echo MISSING; fi; }
has() { [ -e "$1" ] && echo present || echo missing; }

echo "## 0 identity"
p UPTIME "$(cut -d' ' -f1 /proc/uptime)"
p BOOT_ID "$(c /proc/sys/kernel/random/boot_id)"
p SYSTEMD_PID1 "$(cut -d' ' -f1 /proc/1/comm 2>/dev/null)"

echo "## 1 boot / snapshot state"
p BOOTMODE "$(c /sys/power/bootmode)"
p ETC_SNAPSHOT "$(has /etc/snapshot)"
p TMP_HIB_READY "$(has /tmp/.hib_ready)"
p TMP_HIB_POST "$(has /tmp/.hib_post)"
p OPT_ETC_HIB_CAPTURING "$(has /opt/etc/.hib_capturing)"
# ★ 2026-11-28：mount 里 "/" 有两行——initramfs 的 `rootfs ... rw` 在前、
#   真正的 `/dev/root ... ro` 在后。head -1 会取到 rootfs 那行，把 ro 误判成 rw。
p ROOT_LINE "$(mount 2>/dev/null | grep -E ' on / type ' | grep -v '^rootfs ' | head -1)"
p ROOT_LINE_RAW "$(mount 2>/dev/null | grep -E ' on / type ' | tr '\n' '|')"
p OPT_USR_LINE "$(mount 2>/dev/null | grep -E ' /opt/usr ' | head -1)"

echo "## 2 p9"
p P9_SIZE_SECTORS "$(c /sys/class/block/mmcblk0p9/size)"
p P9_MD5 "$(m /dev/mmcblk0p9)"
p P9_IN_SWAPS "$(grep -q '^/dev/mmcblk0p9' /proc/swaps && echo yes || echo no)"
echo "-- /proc/swaps --"
cat /proc/swaps 2>/dev/null
echo "-- end --"
p ZRAM0_DISKSIZE "$(c /sys/block/zram0/disksize)"
p CARD_LED_TRIGGER "$(c /sys/class/leds/card/trigger)"

echo "## 3 stock writers"
p HIBERNATION_SH "$(h /usr/bin/hibernation.sh)"
p MAKE_SNAPSHOT_SH "$(h /usr/bin/make_snapshot.sh)"
p HIB_EXJOB "$(has /usr/bin/hib_exjob)"
p HIB_GUI_RAW "$(has /usr/sbin/hib_gui.raw)"

echo "## 4 di-camera-app unit"
p UNIT_FILE "$(has /usr/lib/systemd/system/di-camera-app.service)"
p UNIT_FILE_SHA "$(h /usr/lib/systemd/system/di-camera-app.service)"
echo "-- ExecStart --"
systemctl show di-camera-app.service -p ExecStart 2>/dev/null
echo "-- ExecStartPre --"
systemctl show di-camera-app.service -p ExecStartPre 2>/dev/null
echo "-- MainPID/Active --"
systemctl show di-camera-app.service -p MainPID -p ActiveState -p SubState 2>/dev/null
p DROPIN_DIR "$(has /etc/systemd/system/di-camera-app.service.d)"
p T2_PREMOUNT "$(has /opt/usr/nx-ks/dicam/t2_premount.sh)"
p T2_C2_SHA "$(h /opt/usr/nx-ks/dicam/di-camera-app.c2)"
p APP_SHA "$(h /usr/apps/com.samsung.di-camera-app/bin/di-camera-app)"
p APP_MOUNTLINE "$(mount 2>/dev/null | grep -c 'com.samsung.di-camera-app/bin/di-camera-app')"

echo "## 5 hooks that decide ordering"
p HIB_READY_UNIT "$(has /usr/lib/systemd/system/hib_ready.service)"
echo "-- hib_post.path --"
cat /usr/lib/systemd/system/hib_post.path 2>/dev/null || echo MISSING
echo "-- hib_post.service --"
cat /usr/lib/systemd/system/hib_post.service 2>/dev/null || echo MISSING
echo "-- hib_post Wants --"
systemctl --all show hib_post.service -p Wants 2>/dev/null
echo "-- hibernation.service enablement --"
systemctl --all show hibernation.service -p WantedBy -p LoadState -p ActiveState 2>/dev/null
ls -l /usr/lib/systemd/system/*.wants/hibernation.service /etc/systemd/system/*.wants/hibernation.service 2>/dev/null

echo "## 6 rescue = which bootmode"
p RESCUE_TARGET "$(has /usr/lib/systemd/system/rescue.target)"
echo "-- rescue.target --"
cat /usr/lib/systemd/system/rescue.target 2>/dev/null || echo MISSING
echo "-- rescue.target.wants --"
ls /usr/lib/systemd/system/rescue.target.wants 2>/dev/null
echo "-- anything writing bootmode --"
grep -rl 'power/bootmode' /usr/lib/systemd /opt/usr/nx-ks 2>/dev/null | head -20

echo "## 7 HIB env (the one gate launch.sh cannot fake)"
p TIZEN_ENV "$(has /run/tizen-mobile-env)"
p HIB_IN_TIZEN_ENV "$(grep -c '^HIB=' /run/tizen-mobile-env 2>/dev/null || echo 0)"
p HIB_IN_ENV_NOW "${HIB:-<unset>}"
echo "-- non-secret keys in tizen-mobile-env --"
sed -n 's/^\([A-Z_][A-Z0-9_]*\)=.*/\1/p' /run/tizen-mobile-env 2>/dev/null | sort | head -40

echo "## 8 eye delivery presence"
p EYE_STATE "$(has /opt/usr/nx-eye-af)"
p EYE_ARMED "$(has /opt/usr/nx-eye-af/armed)"
p EYE_LAUNCH_SH "$(h /usr/local/lib/nx-eye-af/launch.sh)"
p EYE_LIB_NATIVE "$(has /usr/local/lib/nx-eye-af/nxl-eye-af.so)"
p EYE_LIB_YUNET "$(has /usr/local/lib/nx-eye-af/nxl-eye-af-yunet.so)"
# prepare.sh 把 resume unit 装在 /etc/systemd/system/（不是 /usr/lib/...）——查错路径会恒 missing。
p EYE_RESUME_UNIT "$(has /etc/systemd/system/nx-eye-resume.service)"
p EYE_LIB_DIR_MOUNT "$(mount 2>/dev/null | grep -c '/usr/local/lib/nx-eye-af')"

echo "## 9 storage headroom for the rebuild"
df -k / /opt/usr /mnt/mmc /opt/storage/sdcard 2>/dev/null
p SD_INFO_TG "$(has /mnt/mmc/info.tg)"
p SD_NX_CS "$(has /mnt/mmc/nx_cs.adj)"

echo "RESULT=PROBE_DONE"
