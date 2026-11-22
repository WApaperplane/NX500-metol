# wifi5g — NX500 5GHz 只读调查的原始数据

对应文档：`../../docs/current/WIFI_5GHZ_FINAL_2026-11-20.md`
全部为只读取得：驱动 GET 查询、从相机复制的固件文件、公开 FCC 记录。**未发射、未刷写、未拆机。**

## 文件

| 文件 | 内容 |
|---|---|
| `PWRTABLE-RAW.txt` | `0x00fb` 只读 GET 的原始响应（五个子带，全部 `Result=0000`） |
| `POWER-DECODED.json` | 逐字节解析结果：TLV 边界、每信道每速率功率 |
| `POWER-ANALYSIS.txt` | 解析结论与两处更正 |
| `TABLE-LAYOUT.txt` | 固件内功率表的最终布局（39 条 × 17 B） |
| `COMPACT-TABLE-CONCLUSION.txt` | 下载块封装核对（16 B 头、4 字节重叠） |
| `*.compact-5g.bin` | 两个固件镜像各自还原出的 663 B 表，逐字节相同 |
| `verify-blocks.py` | 复算：块边界、表覆盖、39 条信道、按 rate 匹配响应 |
| `extract-compact.cjs` | 从镜像导出紧凑表（含断言） |
| `analyze-power.cjs` | 解析 `PWRTABLE-RAW.txt` |
| `txpwrlimit_get.conf` | 送给 `mlanutl hostcmd` 的结构化 conf（`CmdCode=0x00fb`） |
| `PROBE-REVIEW.md` | 未执行的探测脚本的审查记录、失败矩阵、四轮只读核查 |

## 复现

```bash
node verify-blocks.py 2>/dev/null || python3 verify-blocks.py
node extract-compact.cjs      # 需要 sd8887_uapsta.bin / sdio8887_sdio_combo.bin 同目录
node analyze-power.cjs        # 读 PWRTABLE-RAW.txt
```

固件镜像未随仓库分发（版权）。可从相机 `/lib/firmware/mrvl/` 只读复制，两者 SHA256：

```
sd8887_uapsta.bin        a976a5886089f3159e1ff541ce17ba9148935ce9d87345ac2e36506f394a7b91
sdio8887_sdio_combo.bin  97fafd5b33aad86340663ff5d46ffb132a1c3073963f5729c807c84437f9b5fb
```

## 在相机上取数（只读，安全）

```sh
mlanutl mlan0 hostcmd /tmp/txpwrlimit_get.conf txpwrlimit_cfg_get
```

`SubBand:2=0xNN` 依次取 `0x00`(2.4G) / `0x10` / `0x11` / `0x12` / `0x13`。

## 别碰这些

`mlanutl mlan0 rdeeprom` 会让整机重启（源码里是纯读，实测仍然挂）。
`otpuserdata` / `memrdwr` / `regrdwr` 同属一个 ioctl 家族，同样禁用。
