# Reddit post (EN) — 2026-11-20 / NX500 5GHz investigation

> Subreddits: r/SamsungNX, r/VintageDigitalCameras, r/cameras.
> Flair: "DIY / Repair" or "Discussion".
> Angle: one finding, stated with its limits. No hype, no "I cracked it" — the post says where the
> evidence stops and what would answer it.
> All measurements were read-only. No RF was transmitted, nothing was reflashed, no hardware opened.

---

## Title options

1. NX500: the 5GHz hardware question, mapped as far as read-only evidence goes
2. Samsung locked 5GHz on the NX500 with one driver bit — and the firmware still has all the 5G power tables
3. What I could and couldn't prove about 5GHz on the Samsung NX500

---

## Body

I mod a Samsung NX500 (2015, DRIMe5). This started as a transfer-speed problem — pulling full-size
JPEGs over Wi-Fi tops out around 2 MB/s — and turned into a question about why the camera is 2.4GHz-only
when the silicon clearly isn't.

Short version: **the 5GHz block is a single driver default, the firmware still carries full 5G power
tables, and two related models passed FCC certification for 5G. Whether the antenna and front-end
actually cover 5GHz, I could not prove without transmitting.** So I stopped there.

Everything below is read-only: driver queries, firmware files copied off the camera, public FCC records.

### 1. The block is one variable in the driver

The Wi-Fi chip is a Marvell SD8887 (driver `SD8887-15.68.4.p215`). At load time the firmware reports
`fw_cap_info = 0xff0b`, and per Marvell's `GET_FW_DEFAULT_BANDS` macro that decodes to
**B|G|A|GN|GAC|AAC** — i.e. the firmware advertises 5GHz and 11ac.

The driver then ANDs it with a board mask, `dev_cap_mask = 0xffffcbff`:

```
0xff0b & 0xffffcbff = 0xcb0b  ->  B|G|GN
```

That mask is not just a comment. `mlinux/moal_main.c:297` defaults it to `0xffffcbff` with the note
*"NX500 doesn't supports 5GHz and 802.11ac"*, and the same 4 bytes (`ff cb ff ff`) are present in the
shipped `sd8xxx.ko`. On the camera, `mlanutl mlan0 bandcfg` returns `Infra Band=11 (B G GN)` — bit-for-bit
consistent.

`moal_sta_cfg80211.c:5243` then registers the 2.4GHz band unconditionally, and only registers 5GHz if
`fw_bands` contains `BAND_A`. So the whole 5GHz question sits behind that one filter.

### 2. The firmware still has the 5G tables

This is the part I expected to be missing. It isn't.

A generic host command path (`woal_priv_hostcmd`, `moal_eth_ioctl.c:315`) only checks length — no band
check at all — so a read-only GET (`0x00fb`) can be sent **while the driver is still in 2.4GHz-only
mode**. All five sub-band queries returned success:

| Sub-band | Entries | Channels |
|---|---|---|
| 0x00 (2.4G) | 14 | 1–14 |
| 0x10 | 8 | 36–64 |
| 0x11 | 12 | 100–144 |
| 0x12 | **1** | 144 only (anomalous — expected 149–165) |
| 0x13 | 14 | 183–196 + 7,8,11,12,16,34 |

Then I went looking in the firmware image itself. Both `sd8887_uapsta.bin` and `sdio8887_sdio_combo.bin`
contain the **same 39-entry × 17-byte table** (1 byte channel + 16 bytes power, no count field), and all
35 five-GHz entries returned by the GET match it exactly when read with the right rate index.

So: per-channel, per-rate 5GHz power limits are sitting in the firmware of a camera that cannot use them.

### 3. FCC: the same platform and the same body both passed 5G

| FCC ID | Model | 5GHz |
|---|---|---|
| A3LNX500 | NX500 | none — 2.4GHz only, 11n 20MHz |
| A3LNX1 | NX1 | 5180–5240, 5260–5320, 5500–5700, 5745–5825, up to 80MHz |
| A3LNX30 | NX30 | same four ranges, 20/40MHz |

NX1 shares the platform; NX30 shares the *body* (the NX500 is an NX300 body with NX1 internals).
Note the caveat: NX30 uses an Atheros AR6003, a different chip from the SD8887, so it is evidence that
this body can host dual-band antennas — not that the NX500 uses the same front-end.

### Where the evidence stops

The antenna in the NX500 is a separate module (service manual: checkable *after removal of the shutter*;
on the NX1 it's under the front cover). I could not determine its bandwidth from FCC internal photos
(they only show the mainboard side) or from service-manual parts lists (no standalone Wi-Fi antenna part
number).

That single unknown decides everything: SD8887 is 1x1 with one RF port, so 2.4 and 5GHz share one
front-end and one antenna. If the antenna is 2.4GHz-narrowband, enabling 5GHz means transmitting into a
serious mismatch, with no reliable VSWR protection. That's why I didn't run the experiment — not because
the unlock is hard (it's one insmod parameter), but because the failure mode is irreversible while the
payoff is maybe 1.5–2x on a link that's already limited by retries, not bandwidth.

Two side notes for anyone poking at these cameras:

- `mlanutl mlan0 rdeeprom` **reboots the whole camera** on the NX500, despite being a pure read in
  source. Same family (`otpuserdata`/`memrdwr`/`regrdwr`) — avoid.
- After a Linux reboot, dmesg says `WLAN FW already running! Skip FW download`. The Wi-Fi firmware is
  not reloaded on a soft reboot; only a real power cycle resets the chip.

### Questions for the room

1. Has anyone removed the BT/Wi-Fi antenna module from an NX500 and looked at it? Even a photo of the
   feed point and matching network would settle this.
2. Does a `dnloader.bin` for NX1/NX500 exist anywhere? The service manual's Main PCB Recovery section
   needs it, and as far as I can tell Samsung never shipped it in the NX1/NX500 OSS releases — which
   means the last-resort recovery path is missing a file.

Full write-up and all the raw dumps are in the repo (AGPL-3.0):
https://github.com/WApaperplane/metol

- write-up: `docs/current/WIFI_5GHZ_FINAL_2026-11-20.md`
- raw dumps + repro scripts: `test_server/wifi5g/`
