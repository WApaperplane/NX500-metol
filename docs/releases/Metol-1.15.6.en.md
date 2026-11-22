# Metol 1.15.6

Released: 2026-11-16
One thing only: every remaining **NX-WA / NXWA / 2.88** reference in the project is gone, and the name is Metol everywhere. Functionally identical to 1.15.5.

## 1. Version scheme

| | |
|---|---|
| Before | `2.88.0-nxwa.15.5` (NX-KS lineage + sequence + patch) |
| After | `1.15.6` (major + sequence + patch) |

Mapping:

| Before | After |
|---|---|
| `2.88.0-nxwa.1` … `2.88.0-nxwa.15` | **1.1.0** … **1.15.0** |
| `2.88.0-nxwa.15.1` … `.15.4` | **1.15.1** … **1.15.4** |
| `2.88.0-nxwa.15.5` | **1.15.5** |

NX-KS 2.88 is still the code lineage (see the bloodline line in `VERSION` and `ATTRIBUTION.md`); it just no longer appears in the version string.

## 2. What changed

| Layer | Change |
|---|---|
| Package name | `NX-WA-<ver>.zip` → `Metol-<ver>.zip`; manifest renamed to `Metol-MANIFEST.txt` |
| Camera menu | "版本 v2.88" → "Metol 版本"; "卸载模块 v2.88" → "卸载模块". All four Chinese menus (NX500 / NX1) and both English overlays updated |
| Version popup | `ver.sh` now reads the version from the packaged `VERSION` before showing it, so future releases need no script edit; falls back to a built-in value |
| Docs | `README.md` / `README_EN.md` titles, latest-version line, package naming; `ATTRIBUTION.md` title |
| Runtime | `/tmp/nxwa-tc.lock` → `/tmp/metol-tc.lock`; `/tmp/nxwa-lut-video` → `/tmp/metol-lut-video`; `/tmp/nxwa-pwrlog.{pid,lock}` → `/tmp/metol-pwrlog.*`. All tmpfs, cleared on reboot, nothing to migrate |
| Comments | `f.sh`, `nxstore.sh`, `pwrlog.sh`, `init.sh` |

## 3. Where "nxwa" is deliberately kept

| Location | Reason |
|---|---|
| `/mnt/mmc/nxwa_regtab_20261018` | Hard-coded inside `nx_regtabwrite.arm`. Changing only the shell would split backup and read into two directories; the rename needs a rebuilt binary |
| `/tmp/nxwa_lutload.lock`, `/tmp/nxwa_lutwatch.lock` | Same — hard-coded in `nx_lutload.arm` |
| git branch `nxwa` | Renaming breaks every existing clone and PR for zero benefit |
| Historical tags and release tags | GitHub tags are immutable |
| `docs/archive/`, older `docs/releases/`, `raw8/` | Historical record; rewriting them destroys traceability |
| `ssh-ed25519 … xiaoa@nxwa` | SSH key comment, unrelated to project naming |

The three hard-coded paths above will be handled together with the next binary rebuild.

## 4. Historical releases: titles only

All 18 `NX-WA 2.88.0-nxwa.X` releases on GitHub now carry a `Metol 1.X.Y` title, with the first line of the body updated too. **Tags and asset filenames are untouched** — they are immutable historical records, and renaming them would break existing download links for no gain. The `Metol-` prefix starts with 1.15.6.

## 5. Packages

| Package | Size | md5 | Entries |
|---|---|---|---|
| `Metol-1.15.6.zip` | 10,512,313 B | `352b40b75120243d9c3d80c9f82a53ef` | 401 + manifest |
| `Metol-1.15.6-EN.zip` | 10,511,510 B | `470caec6edc97c76bebee3b664410ef8` | 401 + manifest |

## 6. Upgrade

As usual: extract to the SD-card root → insert the card → wait for the automatic sync → reboot. No functional change, so this one can be skipped; after upgrading, that menu entry reads "Metol 版本" and the first popup shows the current version number.
