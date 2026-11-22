# Metol

An enhancement mod for the **Samsung NX500 / NX1** (Tizen / DRIMe5), built on the community
[`ottokiksmaler/nx500_nx1_modding`](https://github.com/ottokiksmaler/nx500_nx1_modding) upstream.

**Repository** [WApaperplane/metol](https://github.com/WApaperplane/metol) (a fork; default branch `nxwa`) ·
**License** AGPL-3.0 (lineage in [ATTRIBUTION.md](ATTRIBUTION.md)) ·
**Download** [Releases](https://github.com/WApaperplane/metol/releases)

> 中文版见 [README.md](README.md)

**Latest 1.15.6** (2026-11-16) — bilingual release: every version ships two packages,
`Metol-<ver>.zip` (Chinese) and `Metol-<ver>-EN.zip` (English menus / English NXUI panel / English
README), with release notes in both languages.

---

## 30-second start

1. Unzip `Metol-<ver>-EN.zip` to the **SD-card root** → insert the card → "install complete" → the camera reboots.
2. **Turn Bluetooth on once by hand** (menu → Settings → Enable Bluetooth) — the mod's boot hook is initialised by it.
3. Learn four keys:

| Key | Opens | Note |
|---|---|---|
| **EV + WiFi** | **LUT panel** (LUT / FILMLAB tabs) | tap to switch, **no half-press needed**; tap the LUT tab again while already on LUT to flip domain (standard ⇄ V-Log) |
| **EV + AEL** | **V-Log curve page** | camera-level gamma switch. Also reachable via the menu (double-tap EV → Custom Func. → V-Log Curve); this key is the shortcut |
| **Double-tap EV** | Main menu (NXUI paged UI) | items are listed in [Camera menu](#camera-menu) |
| **Half-press** | Closes the current panel | the applied look **stays** after closing |

> **FilmLab is now part of the LUT panel** (since 1.15.7): there is no separate strip entry any
> more — the 79 recipes live in the FILMLAB tab.
> **Inside an open panel**, a plain WiFi / AEL press switches tabs (LUT ⇄ FILMLAB); hold EV for
> the combos.
> All combos are "hold EV, then press the other key". A plain WiFi press is still the camera's own WiFi toggle.

## Troubleshooting: read the log before touching anything

### Four recovery commands

```sh
sh /opt/usr/nx-ks/nxui.sh disable    # UI misbehaving -> back to the legacy mod_gui menu
sh /opt/usr/nx-ks/lut.sh off         # wrong colours -> back to the stock LUT
sh /opt/usr/nx-ks/vlog.sh off        # V-Log curve -> back to stock gamma
sh /opt/usr/nx-ks/nxui.sh diag       # is the panel usable? all five checks at once
```

`nxui.sh diag` prints `bin / model / disabled / enabled / available`. `available: no` has only three
causes: the binary is missing, the model is NX1, or someone ran `disable`.

### Where the logs are

| Path | Written by | Survives power-off? |
|---|---|---|
| `/mnt/mmc/nx-ks/pwrlog.txt` | `pwrlog.sh` (15.1+, one line per 30 s, plus a `BOOT` record) | yes — SD |
| `/mnt/mmc/lut/state/*.log` | `lut.sh`, one per step (write / view / still / bv / lutptr / kick) | yes — SD |
| `/mnt/mmc/filmlab/last.log` | last recipe-apply context | yes — SD |
| `/opt/usr/nx-ks/onboard.log` | install self-check (telnet / ssh / nxui diag / IP) | yes — p10 |
| `/tmp/nxui.log` | panel `READY / ACTION / BACKEND ok=…` lines | no |
| `/tmp/db.log` | dropbear (did ssh start, and why not) | no |
| `/tmp/metol-lut-video/watch.log` | video LUT watcher | no |

A camera that **powers itself off** is the one case that must be captured in advance: the last
`pwrlog.txt` line (`up=… pwr=… ssh=… ui=… lut=…`) plus `pwrlog.sh dump 60` is the whole story.
Everything under `/tmp` is gone at that moment.

### Symptom → where to look

| Symptom | Look at |
|---|---|
| EV+AEL shows the legacy NX-KS popup | `nxui.sh diag`; `available: no` means binary missing / NX1 / disabled |
| Panel taps do nothing, stuck on "sending" | tail of `/tmp/nxui.log` (`BACKEND ok=0`) + the matching `lut.sh` state log |
| A new LUT changes nothing | `lut.sh status`; then half-press once (p7's own load event) |
| Colours look off — is it the LUT? | compare before/after `lut.sh off`; `lut.sh stock` exports the factory table |
| The camera powers itself off | `/mnt/mmc/nx-ks/pwrlog.txt` → `sh /opt/usr/nx-ks/pwrlog.sh dump 60` |
| SSH unreachable | menu `IP: … [SSH on]`; `sh /opt/usr/nx-ks/ssh_hint.sh --start`; `/tmp/db.log` |

## Install

1. Put `info.tg`, `nx_cs.adj`, `install.sh` and `scripts/` at the **root of the SD card** (or unzip `Metol-<version>.zip` there).
2. Insert the card → it installs automatically and shows "install complete".
3. **Turn Bluetooth on once by hand** — the mod's boot hook is initialised by it.

- Firmware: NX500 **1.12** / NX1 **1.41**; anything else is refused.
- Two packages: `Metol-<ver>.zip` (Chinese) and `Metol-<ver>-EN.zip` (English menus, English NXUI panel, English README). Identical features. Installing the other package swaps the language back; `nxui/lang` is overwritten by the package.
- Inserting the card again = **incremental sync** (overwrite only, never delete, never auto-uninstall); uninstall from the camera menu (`Uninstall`).
- Install / sync / WiFi push / rollback: [SYNC.md](SYNC.md) (bilingual).

## Remote access (SSH)

There is exactly one remote path: **dropbear ssh on port 22** (shipped, started by `init.sh`).
telnet and FTP were retired on 2026-11-02; the `telnetd` binary is **not in the release zip**
(2016 Samsung original, intentionally untracked).

| Method | Command | Requirement |
| --- | --- | --- |
| Blank password | `ssh root@<camera-ip>` | just press Enter (dropbear `-B`); some clients refuse blank passwords |
| Public key (recommended) | append your `id_ed25519.pub` to `/opt/usr/nx-ks/dropbear/authorized_keys`, then `sh /opt/usr/nx-ks/ssh_hint.sh --start` | log in once with the blank password first |
| Menu | camera menu → `IP: 192.168.x.x [SSH on]` | shows state; pressing it pops the login string and starts ssh if it is down |

The host key `dropbear_ed25519_host_key` is generated on first boot (kept on p14, survives reflashing).
For troubleshooting see `/opt/usr/nx-ks/onboard.log` and `/tmp/db.log`.

---

## Camera menu

Six entries on the main page plus one status row. ★ = added by this project,
"upstream" = inherited from NX-KS 2.88 with unchanged behaviour.

### Main page

| Item | Function |
|---|---|
| Time-Lapse → | see "Time-lapse" |
| Custom Func. → | see "Custom functions" |
| Focus + Profiles → | see "Focus" |
| Settings → | see "Settings" |
| Hibernate | sleep immediately (`hibernate.sh`, upstream) |
| Remote Ctr ☑ | web gallery / remote control (nx-rc.sh, upstream) |
| `IP: x.x.x.x [SSH on]` | ★ SSH state; tap to pop the login string, starts dropbear if it is down |

### Time-lapse

| Item | Function |
|---|---|
| Wake Lapse | wake at a set time and start shooting (`wakelapse.sh`, upstream) |
| Ramping → | exposure ramping inside a time-lapse: total duration / interval / start & end exposure / ramp start & length / start delay; `开始` runs `ramping.sh` (upstream) |
| Mod Lapse | `mod_lapse` (upstream binary) |
| Coming soon | placeholder |

### Custom functions

| Item | Function |
|---|---|
| Remote Save ☑ | remote-save toggle (`rem.sh`, config in `remote.cfg`, upstream) |
| Video Bitrate → | five bitrate slots (Pro4K / ProHD / Hq4K / HqHD / HqVGA) plus the current value, template `gui_br.tp` (upstream) |
| ZoomZoom | set a delay → take a shot → zoom back to wide: a zoom-burst tool (`zoomzoom.sh`, upstream) |
| Resolutions → | see "Resolution mapping" |
| Batch Rec | continuous recording in minute-sized batches, optionally with a hidden UI (`brec.sh`, upstream) |
| HDR | five shutter steps (65 / 92 / 119 / 146 / 200) in M mode, five frames, **no in-camera merge** (`1HDR.sh`, upstream) |
| LUT Filter | ★ opens the NXUI LUT panel (same as EV+WiFi) |
| V-Log Curve → | ★ camera-level gamma: V-Log toggle / restore stock / curve status / gate check (usually via **EV + AEL**) |
| `Metol 版本` | version popup (`ver.sh`) |
| Camcorder ☑ | shutter button becomes a record button (`camcorder_mode.sh` → `shutter_to_rec`, upstream) |

### Focus + Profiles

| Item | Function |
|---|---|
| Focus Stacking | automatic focus stepping by count / interval (`fstack.sh`, config on SD: `/mnt/mmc/scripts/focus_stack.cfg`, upstream) |
| Focus Buttons | long-press to store a position, tap to recall (`/mnt/mmc/scripts/af_positions.txt`, upstream) |
| Profiles → | save / load `EV+UP`, `EV+DN`, full backup (`save_UP.sh` / `EV_UP.sh` / `FullSave.sh` …, upstream) |
| Focus-Pull Speed | pull speed 1–255 (writes the `fps` environment variable, `fpspeed.sh`, upstream) |
| My Checkbox / My Button | template placeholders for your own scripts (`cCheckbox.sh` / `cButton.sh`, upstream) |

### Settings

| Item | Function |
|---|---|
| Silent Shutter ☑ | electronic shutter `ADJUSTSHUTTERTYPE 0x750001` (`rolling_shutter.sh`, upstream) |
| OFF Menu Button ☑ | disables the MENU key's default OSD behaviour (`z_button.sh`, upstream) |
| No Rec Limit ☑ | removes the recording time limit (`prefman 0xc2d9=1`, upstream) |
| Refocus on Boot ☑ | refocus to the position stored before hibernation (`refocus.sh`, upstream) |
| No Lens Release ☑ | release the shutter without a lens (`prefman 0xc2dd=1`, upstream) |
| Enable Bluetooth ☑ | restarts the Bluetooth service (`bt.sh`, upstream) |
| Overlay ☑ | starts the `onscreen_ov` overlay (upstream) |
| Uninstall | uninstalls the mod (`uninstall.sh`) |

### Resolution mapping

Maps a capture mode (UHD / DC / VGA) onto a different resolution. The eight switches are mutually
exclusive — enable **one per mode**:

| Mode | Can be mapped to |
|---|---|
| UHD | 2.5K full width (2560×1440) / DC 4K (4096×2160) / FHD (1920×1080) |
| DC | 2.5K / UHD / FHD |
| VGA | UHD / DC 4K |

The switches are stored in the camera preferences; verify the actual output in the viewfinder.
Upstream NX-KS capability.

---

## FilmLab recipes (the LUT panel's second tab)

FilmLab has **no separate entry**: open the panel with `EV + WiFi` and tap the `FILMLAB` tab (or
press WiFi / AEL inside the panel to switch layers). It is two tabs and two independent states —
switching layers never touches the other layer's camera state.

| Family | Examples (79 total: 71 colour + 8 B&W) |
| --- | --- |
| Colour negative | Portra 400/800, Gold 200, Ektar 100, Superia 400, ProImage 100, Vista 200 … |
| Slide | Velvia 50/100, Provia 100F/400X, Ektachrome E100, Kodachrome 25/200 … |
| B&W | TriX 400, HP5 Plus, Delta 3200, T-MAX 100/400, Acros 100, XP2 Super … |
| Cinema | CineStill 800T/50D, Vision3 250D/500T, Double-X 5222, Eterna 500T |
| Stylised | Teal-orange / warm-faded / cold / bleached / high-key / low-key / sepia / bleach-bypass / blue hour / X-Pro |
| Camera looks | Ricoh GR (Positive / Negative / Retro / Bleach / Cross-process), Pentax Custom Image (Vivid / Reversal / Bleach Bypass / Muted / Satobi / Gold) |

- **Recipes need no code change**: edit `/mnt/mmc/filmlab/recipes.json` → copy back to SD → reopen the menu (it rebuilds itself). The authoritative source in-repo is `scripts/filmlab/recipes.json`; host-side CLI: `test_server/filmlab/flab.py add|edit|rm|lint|deploy`.
- A recipe is a Picture Wizard 7-value vector (R/G/B gain + HUE/SAT/SHARP/CONTRAST), always written into the **Custom 1** slot.
- **The full 7-dimension push needs the A2 firmware** (`patch-a2/`, see [A2 p7 patch](#a2-p7-patch-optional)). On stock firmware R/G/B/HUE need one confirmation in **Picture Wizard → Custom 1** — a structural limit of p7's normaliser domain (`0x100–0x12e`), not a script bug.
- Honest limits: WB is not part of a recipe (use the camera's white balance); a recipe is a global colour vector, it cannot do highlight roll-off or zone-based exposure; NX1 is not enabled (the prefman offsets were only proven on NX500 1.12).
- One criterion only: `sh /opt/usr/nx-ks/filmlab.sh check` reads the seven values the ISP is actually using (read-only, zero risk). **"The value changed" ≠ "the value is consumed"**.
- There is also `scripts/nx-rc/MOBILE_MOBILE.sh` (double-tap WiFi = rotate to the next recipe with no UI) — not part of the install chain; copy it to `/opt/usr/nx-ks/` if you want it.

## 3D LUT

| Item | Status |
| --- | --- |
| Table format | `4913×4B {Cr,Cb,Y,pad}`, index `((Y*17+Cb)*17+Cr)*4`, slot 19,712 B, stride `0x4D00` (closed on-device 10-19; the older RGB naming is retired) |
| Retarget | Write 19,712 B into the work slot (A18 `0x81119F00`) + point **all four** registry tables at it + **auto-load after frame end** (`nx_lutload.arm`, no half-press) |
| Registry tables | `View4` (4 entries, colour space × screen colour) + `Still6` (6) + `BV12` (12) — **none may be skipped**, or you get "viewfinder filtered, photo not" |
| Import | `.cube → 19,712 B slot table` via on-camera `cube2lut.arm`, or host-side `test_server/isp/nx3dlut.py` |
| Generation basis | Built for the **measured camera input domain**: response slope `log2(code)/EV = 0.809`, not sRGB's 0.45 (a 1.8× gap). Tables built on the sRGB assumption get crushed twice in the shadows. Of the 86 frozen tables, 47 carry over from the previous release (34 of them regenerated against the real response: 25 G'MIC + 9 recipe tables) and 39 come from the 2026-11-08 community batch |
| Live view / stills | Live view reads only `View4`; stills have **two mutually exclusive branches** (screen colour on → `BV12`, off → `Still6`) and both must be retargeted |
| Video | After `apply`, just press record — **the very first frame already carries the table** (p7's 23-entry slot pointer table is redirected to A18); `off` stops the watcher. **A 4-minute continuous take was verified**; other frame-rate combinations are untested |
| Panel close | **Does not restore**: `Save & close` / half-press only close the panel, the table stays live. To go back, use `Factory` in the panel or `lut.sh off` |
| Limits | Pure memory operations — **power cycle restores stock** (boot-persistent needs an image patch). Colour is trustworthy in sRGB; AdobeRGB only guarantees "it applies" |
| Load sequence | ⛔ Writing `+0x00C` yourself requires all 8 steps: `OnOff` → clear/set Acc → address → Cfg channel bits → bit8 pulse → **reset Acc at the end**, plus waiting for the preview frame to end. Address + pulse alone corrupts the picture — that is the real reason behind the 10-08 "no-go" note |

### Two input domains: standard ⇄ V-Log

The LUT panel has exactly two tabs: `● LUT` and `FILMLAB`. V-Log tables do **not** get a third
tab — they write the same A18 slot as normal tables; only the input domain the table was built
for differs.

| Where you are | What tapping the LUT tab does |
|---|---|
| FilmLab layer | Returns to the LUT layer (domain unchanged) |
| **Already on the LUT layer** | **Flips the domain: standard ⇄ V-Log**; the tab becomes `● LUT·VLOG` |

Flipping only reloads one list (`/tmp/nxui-look-vlog.rec`) — no A18 write, no DMA — and each
domain remembers its own selection. Two directories:

| Domain | Directory | Table input domain |
|---|---|---|
| Standard | `looks/` | display-referred (camera JPEG) — usable anytime |
| V-Log | `looks/_vlog/` | V-Log encoded — **correct only while V-Log gamma is active** |

> The V-Log curve engine lives in `$D/gamma/` on the body and is not bundled. Without it,
> selecting a V-Log table is a double mismatch (the table assumes dark log footage, the camera
> feeds it normal JPEG) ⇒ under-exposed and washed out. Use the standard domain instead.
>
> How to bake third-party V-Log creative tables (Lumix V-Log LUTs etc.) into standard-domain
> tables: see
> [`docs/current/LUMIX_VLOG_IMPORT_2026-11-20.md`](docs/current/LUMIX_VLOG_IMPORT_2026-11-20.md).
> **Names are capped at 12 characters** — the row is 5 × 133 px and clips from the 13th.

### Library and blending

| Capability | Command |
| --- | --- |
| Library | `lut.sh list [--fav]` / `fav` / `move up\|down\|top` / `del` / `index` / `stock` |
| Cross-fade | `lut.sh mix <A> <B> <0-100>` |
| Strength of one look | `lut.sh strength <name> <0-100>` (0-end = stock A17; run `lut.sh stock` once first) |
| **Two looks, two strengths** | `lut.sh blend <A> <r 0-100> <B> <s 0-100>` — `r` = A/B ratio, `s` = total strength |
| Video LUT | No separate switch: starts with a successful `apply`, stops on `off`. Diagnose with `lut.sh video on\|off\|status` |

Order and favourites live in `lut/state/order` on the card (one name per line, `*` = favourite).
Removal is a **move to `looks/_trash/`**, never an `rm` — deleting a duplicated name on exFAT
releases the cluster chain and corrupts the siblings.

#### Frozen library and cleaning out old tables

The 86 bundled tables are the **frozen set** (fixed on 2026-11-11 against what was actually kept on the camera); the roster is `looks/MANIFEST.txt` (md5 + size).
The 80 names from earlier batches are listed in `looks/obsolete.txt` — unzipping only overwrites,
never deletes, so without this they stay on the card and the panel mixes old and new.

```sh
sh /opt/usr/nx-ks/lut.sh verify          # against the roster: MISSING / CHANGED / EXTRA
sh /opt/usr/nx-ks/lut.sh prune --dry-run # see what would move
sh /opt/usr/nx-ks/lut.sh prune           # move old batches to looks/_trash/ (move, not delete)
```

`EXTRA` is not necessarily an error — your own looks count as extra. `prune` only touches names
listed in `obsolete.txt`; anything else is left alone, and mistakes are recoverable from
`looks/_trash/`. After changing the library (add / remove / edit any table) you must re-run
`python tools/lutgen/make_manifest.py` and commit it, otherwise `verify` reports `CHANGED` on the
next machine — deliberately: a library change has to be an explicit act.

Blending is **normalised weighting**: `r = B/(A+B)` picks the mix, `s = min(A+B, 100)` the strength,
`T = stock + s·(mix(A,B,r) − stock)`. Both at 100% therefore lands on the midpoint instead of
clipping. See [`docs/current/DUAL_STRENGTH_2026-11-02.md`](docs/current/DUAL_STRENGTH_2026-11-02.md).

### Panel keys (NX500 hardware keys, all measured)

| Key | Browse | Edit | Mix |
| --- | --- | --- | --- |
| Up / Down · EV dial | page | — | page |
| Left / Right | page | — | pick A / B focus |
| Centre OK | apply & close | done | apply blend |
| Play ▶ | enter edit | — | — |
| Long-press a row | enter edit | — | — |
| Fn | enter mix | — | leave mix |
| Top-right dial | page | — | adjust focused strength (5%/step) |
| BACK · MENU | close | close | close |

> Only the edit state changes the library. A/B is set with the button row
> (`Set A` / `Set B` / `Apply` / `Exit`); leaving the mix state does **not** apply.
> Keys are overridable via `$NXUI_STATE_DIR/keys.conf`, one `<keysym> <action>` per line, `none` unbinds.

```sh
sh /opt/usr/nx-ks/lut.sh list | begin | apply <name> | off | status   # looks live in /mnt/mmc/looks/
sh /opt/usr/nx-ks/lut.sh video on|off|status                          # recording watcher; normally automatic
```

Details: [`docs/current/LUT_RETARGET_LIVE_2026-10-18.md`](docs/current/LUT_RETARGET_LIVE_2026-10-18.md) · panel design: [`docs/current/LUT_UI_2026-10-18.md`](docs/current/LUT_UI_2026-10-18.md)

## V-Log curve

V-Log is a **camera-level gamma**, not a filter: with it on, both the viewfinder and the final
image turn into a flat log-looking grade that leaves headroom for post.
Press **EV + AEL** to open it; the menu entry is kept too (double-tap EV → Custom Func. → V-Log Curve).

| Item | Status |
| --- | --- |
| Entry | **EV + AEL** (hold EV, press AEL); menu `Main menu → Custom Func. → V-Log Curve` (`gui_vlog.NX500`: V-Log toggle / restore stock / curve status / gate check) |
| What it changes | the gamma-side curve — viewfinder and final image together; **it does not touch the 3D LUT** |
| Gate check | requires A18 back to stock (`lut.sh off`), neutral PW, no USB inserted — otherwise it refuses; details land in `/tmp/nx-p0/vlog-gate.log` |
| Relation to LUT | **V-Log is the foundation**: once log is on, the judgement baseline for LUT and FilmLab changes. The two are mutually exclusive — `lut.sh off` first, then enable V-Log |
| Engine | the curve engine (RC5 `gamma-reload.sh`) lives at `$D/gamma/` on the body and is **not bundled**; without it the menu reports it missing |
| Rollback | `vlog.sh off` restores stock gamma; a reboot does it too (runtime only) |

```sh
sh /opt/usr/nx-ks/vlog.sh status    # current curve state (NXL_GAMMA_RELOAD1 block)
sh /opt/usr/nx-ks/vlog.sh on|off|toggle
sh /opt/usr/nx-ks/vlog.sh gate      # gate check, details in /tmp/nx-p0/vlog-gate.log
```

### With V-Log on: how to reach V-Log LUTs

The LUT panel has **two input domains**, each a directory:

| Domain | Directory | What the tables expect |
| --- | --- | --- |
| Standard | `looks/` | normal camera JPEG — usable anytime |
| V-Log | `looks/_vlog/` | V-Log footage (log input) |

**How to switch**: `EV + WiFi` opens the panel → while already on the LUT layer, **tap the LUT tab
once more**; it turns from `● LUT` into `● LUT·VLOG` and the list switches to `_vlog/`. Tap again
to go back. Flipping only reloads a list — no A18 write, no DMA.

> ★ **V-Log-domain tables are correct only while V-Log gamma is active.** Selecting one without
> log is feeding log tables to a normal image — under-exposed and washed out, and it looks like
> "the table itself is broken".
>
> No engine, but you still want the flat log look: bake the log curve into the 3D LUT's **output**
> with `tools/lutgen/bake_log.py`. For third-party V-Log creative tables (Lumix etc.), see
> [`docs/current/LUMIX_VLOG_IMPORT_2026-11-20.md`](docs/current/LUMIX_VLOG_IMPORT_2026-11-20.md).

> History: the old 19-node TC curve (Stock / Log / Flat / High Contrast) was removed from the menu
> on 2026-11-20; `tc.sh` and `gui_tc.NX500` remain on the body as backups. It was dropped because
> it cannot be attributed when stacked with V-Log / 3DLUT, and it only redistributes 256 code
> values without adding dynamic range.

## A2 p7 patch (optional)

The firmware prerequisite for the "all seven PW dimensions in one push". Not flashing it breaks
nothing — R/G/B/HUE simply need one confirmation in Picture Wizard.

| Item | Value |
|---|---|
| Image | `patch-a2/p7_a2.bin` (shipped, 12,075,648 B, md5 `712c6fd0a49371ec05ce94c367b0d5c7`) |
| Target | `/dev/mmcblk0p7` (~30 MB, **a wrong partition is unrecoverable**) |
| Channel A | unzip → write p7 over ssh with `dd` (~1 s) |
| Channel B | Release asset `slp_a2_p7_v117.bin` (352 MB) renamed `nx500.bin`, flashed through the official updater |
| Activation | **normal power off → power on**; ⛔ never `systemctl restart di-camera-app` |
| Rollback | write the `p7_backup_*.bin` back, or reflash official 1.12 |

Full procedure (ssh wording, backup, read-back criteria, split comparison, rollback):
[`fw/v1.12/a2/README-A2_EN.md`](fw/v1.12/a2/README-A2_EN.md) ([中文](fw/v1.12/a2/README-A2.md)).
An official firmware upgrade rewrites p7 ⇒ **re-flash A2 afterwards**.

## Layout

| Path | What |
| --- | --- |
| `install.sh` `info.tg` `nx_cs.adj` | SD-card trigger trio (install / incremental sync) |
| `scripts/` | All modules → synced to the camera's `/opt/usr/nx-ks/` |
| `scripts/filmlab.sh` · `scripts/filmlab/recipes.json` | FilmLab engine / authoritative recipe library (79) |
| `scripts/EV_AEL.sh` · `scripts/EV_MOBILE.sh` · `scripts/EV_EV.sh` | The three key-combo entry points (V-Log curve / LUT panel / main menu) |
| `scripts/nxui.sh` · `scripts/nxui/` | NXUI launcher + panel binaries (`nxui.arm` CN / `nxui_en.arm` EN) + fonts + `lang` |
| `scripts/lut.sh` · `scripts/lut/` | 3D LUT entry + toolchain (`cube2lut` / `nx_slotwrite` / `nx_regtabwrite` / `nx_regtabdump` / `nx_lutload` / `nx_lutptr` / `lutmix`) |
| `scripts/lut_video.sh` | video LUT watcher (follows `apply`/`off`, state in `/tmp`) |
| `scripts/vlog.sh` · `scripts/gui_vlog.NX500` | V-Log curve toggle and gate check (engine must be installed at `$D/gamma/`) |
| `scripts/tc.sh` · `scripts/tc/*.tcy` | TC curve writer and library — **retired**; menu entry removed, kept as backup only |
| `scripts/ssh_hint.sh` · `scripts/dropbear/` | SSH state hint + dropbear itself |
| `scripts/pwrlog.sh` | power-off forensics sampler (one line per 30 s to SD) |
| `scripts/gui_*.NX500` | native menu pages (`i18n/en/scripts/` is the English overlay) |
| `tools/tcgen/` · `tools/lutgen/` | host tools: curve generation / read-only chain verification / LUT generation and audit |
| `test_server/` | host-side dev environment (API mocks, toolchain, panel sources and `make_en.py`) |
| `uninstall_pkg/` | camera-menu uninstall entry |

## Docs

- [`docs/README.md`](docs/README.md) — **documentation index** (`current` / `archive` / `evidence`)
- [`docs/current/HANDOVER_2026-10-16.md`](docs/current/HANDOVER_2026-10-16.md) — entry point: conventions, dead ends, gates, order of work
- [`docs/current/NXUI_ENABLE_GATE_2026-11-10.md`](docs/current/NXUI_ENABLE_GATE_2026-11-10.md) — why the UI "did not start" and what `diag` checks
- [`docs/current/UI_LUT_SCOPE_2026-11-20.md`](docs/current/UI_LUT_SCOPE_2026-11-20.md) — LUT's two input domains: tab re-tap, queue index invalidation, edit-mode limits
- [`docs/current/LUMIX_VLOG_IMPORT_2026-11-20.md`](docs/current/LUMIX_VLOG_IMPORT_2026-11-20.md) — baking third-party V-Log tables into standard-domain tables (S2V composite, dedup, name limit)
- [`docs/current/VIDEO_LUT_FIRSTFRAME_2026-11-07.md`](docs/current/VIDEO_LUT_FIRSTFRAME_2026-11-07.md) — video first frame carries the LUT
- [`fw/v1.12/a2/README-A2_EN.md`](fw/v1.12/a2/README-A2_EN.md) — A2 p7 flashing guide

> Conclusions in old documents (e.g. "View/Still cannot be separated", "user space cannot write
> the LUT") have been overturned; see [`ERROR_CORRECTIONS_2026-10-07.md`](docs/current/ERROR_CORRECTIONS_2026-10-07.md).

## Boundaries

Being accurate matters more than looking complete. Every row below maps to an on-device or static
verification, none is "should work".

| Dimension | ✅ Works | ❌ Not / unverified |
|---|---|---|
| Platform | NX500 firmware **1.12** (all features verified on device) | NX1 1.41 installs, but LUT / FilmLab / NXUI are not enabled there; other firmware versions are refused |
| Colour | sRGB output: viewfinder and JPEG colours are trustworthy (basis of the 86 bundled tables, 34 of them generated against the measured input domain) | **AdobeRGB applies but is not calibrated**; in-camera smart filters (enum 14–18) take another 24-step dynamic path and are not covered |
| Carriers | **JPEG stills + viewfinder + video (first frame carries the table)** | **RAW (`.SRW`) is not promised to be baked**; the ground is 8-bit 4:2:0 (10-bit unreachable, the built-in gamma interface has no consumer); a 4-minute take has been verified, frame-rate combinations are untested |
| V-Log | **Camera-level gamma switch** — viewfinder and final image together; gated on A18 back to stock + neutral PW + no USB | The curve engine (RC5 `gamma-reload.sh`) is **not bundled** and must be placed in `$D/gamma/`; once log is on, LUT / FilmLab baselines change and tables must be re-picked |
| Activation | LUT auto-loads after a write (waits for frame end, no keypress) | **Not boot-persistent** — power cycle restores stock; persistence needs an image patch |
| Triggering | `nx_lutload.arm once` replays p7's own load sequence, aligned on the `CORE RSZ_OUT1_EOF` frame event | With the screen off or `di-camera-app` gone the ISP does not run, so the DMA-done interrupt never arrives — fall back to "half-press once" |
| Shape | No flashing, no partition rewrite, fully reversible (A2 is the only optional p7 write) | Without touching the firmware partition, a reboot restores stock |

> It is **not** a colour-management system (no input/output profile conversion), **not** a
> WYSIWYG promise (viewfinder and still pipelines differ), **not** a crack (no region unlock, no
> recording limit change). It edits two ISP tables: the 3D look-up table and the gamma curve.

## Hard limits (p7 / firmware, not script bugs)

- **All-seven-dimension PW push needs the A2 firmware**; on stock firmware values outside the normaliser domain are rejected.
- **p7 has no UI** (zero hits for Font/DrawText/Dialog/Widget) ⇒ the interface can only live on the Linux side.
- **Combos of three or more keys are unavailable** (the kernel consumes keys first) ⇒ only "hold EV + one key" works; long-press is detected by NXUI from press/release timing, not by hardware.
- **V-Log only changes gamma shape, not bit depth**: there is no 10-bit ground in 8-bit 4:2:0; `MOVIE_GAMMA_CONTROL` / `IQ[180]` has no consumer and 10-bit encoding does not exist in this firmware.
- Factory LUT tables are **all stylistic** — "writing identity gives neutral" is not a valid test.

> The film-simulation engine and `.cube` assets live in [nx500-filmsim](https://github.com/WApaperplane/nx500-filmsim) (public).

## Credits

- **[NoireSchwarz/NX500_SRW_reprocessing_6.5K-oversampling](https://github.com/NoireSchwarz/NX500_SRW_reprocessing_6.5K-oversampling)**
  — research and fitting of the Panasonic V-Log ⇄ Standard curves. Without that curve, V-Log
  creative tables would be nothing but double-mismatched grey on the NX500, and the baking
  pipeline in 1.15.7 would not exist.
- **Panasonic** — original author of the 35 `varicam_official` tables and the 22
  `lumix_color_lab` ones (VariCam LUT Library / Lumix Color Lab).
- **[changyun233/Lumix-V-log-LUTs](https://github.com/changyun233/Lumix-V-log-LUTs)**
  — curation and previews of 107 V-Log creative tables (Booz / Changyun / Hamburger / Lucas Hu /
  Lumix Color Lab / VariCam official). These tables are **not bundled**; get them from the
  original link.
- **[ottokiksmaler/nx500_nx1_modding](https://github.com/ottokiksmaler/nx500_nx1_modding)**
  — upstream bloodline.
