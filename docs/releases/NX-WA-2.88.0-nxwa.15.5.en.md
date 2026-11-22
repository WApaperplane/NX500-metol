# NX-WA 2.88.0-nxwa.15.5

Released: 2026-11-16
Three things in this release: the album stops transcoding thumbnails and slices the embedded ones instead, persistent writes become recoverable commits, and writes that cannot be confirmed are now refused outright instead of "warn and continue".

## 1. Album thumbnails: slice the embedded images, two orders of magnitude faster

Every NX500 original already carries two derived images:

| Use | Source | Size | Bytes |
|---|---|---|---|
| Grid | EXIF IFD1 thumbnail | 320×216 | 3.6–14 KB |
| Lightbox | MPF appended image | 1616×1080 | 391–832 KB |

The new `nxthumb.arm` (6,032 B, pure syscalls, no libc) parses IFD/MPF and writes those byte ranges straight to stdout — **no decode, no pixel allocation, no locking, no cache.**

Measured on hardware (13.9 MB original, end-to-end over Wi-Fi, peak RSS sampled from `/proc`):

| Operation | Time | Peak RSS | Output |
|---|---|---|---|
| Grid slice | **148 ms** | <1 MB | 12 KB |
| Lightbox slice (MPF) | **759 ms** | <1 MB | 811 KB |
| Grid transcode (cold) | 7,970 ms | 8.7 MB | 22 KB |
| Lightbox transcode (cold) | 7,452–20,030 ms | 31.2 MB | 246 KB |
| Prewarm 60 photos | **1,990 ms** (was ~28 min) | — | 60/60 |

- **The lightbox uses MPF by default**: ~560 KB more transfer to avoid a 7.5–20 s wait per new photo. Fall back with `touch /tmp/nx/thumb/mpf-off`.
- **Portrait shots are not rotated in pixels** — orientation lives only in EXIF. When orientation≠1 the slicer prepends a 36-byte minimal APP1 segment, and the browser rotates it via `image-orientation: from-image`. No frontend change needed.
- **The transcode path is fully retained** (PNG, foreign photos, corrupt files, `mpf-off`), now with a memory admission check: below **40 MB** of `MemFree + Cached`, transcoding is refused — a single OOM kills the camera daemon and only a battery pull recovers it.

## 2. Persistent writes become recoverable commits

The 11-14 on-device experiment confirmed this card's exFAT does **not** replace an existing name on rename: every overwrite leaves a stale duplicate entry (10 writes → 11 entries). The previous countermeasure was "rm first", which leaves a window where the target does not exist at all — a power loss there loses the file.

Writes now use a three-file staged commit: `target` / `target.nx-new` (staged) / `target.nx-prev` (previous). Order: sync → rename target to .nx-prev → sync → rename new to target → sync → remove .nx-prev. Any failure rolls back, and readers call `nx_recover`, which restores the previous version whenever a sidecar is present.

- **Global flock**: the list/fav/move/index/del verbs of `lut.sh`, the move/del/restore/backup verbs of `recipelib.sh`, and the PW direct push in `filmlab.sh` each take a lock, so the panel and the CLI cannot collide.
- `nxui.sh` restores `recipes.json` from `recipes.json.nx-prev` on startup before reading it.
- **Regression tests rewritten**: a busybox shim injects "half-written cp" and "mv failure", locking down the success, rollback and recovery paths.

## 3. Refuse instead of warn-and-continue

- **FilmLab stops when it cannot read the active field.** `pw_preenter_custom1` used to log a warning and push anyway, and the camera attributed the values to whichever field the user was on (the report from ssam87: "custom 1 and 2 were both affected"). A failed read or an unrecognised field is now a hard `REJECT` — no `pwsend`, no `prefman`.
- **The active field is re-read before every push**; the panel's opening cache is no longer trusted. Preview no longer touches the profile pointer.
- **`lut.sh order_sync` aborts on failure**: a failing `mkdir` or `ls` used to continue into the write path; it now returns non-zero and `die`s.
- **`recipelib.sh` aborts when the backup fails**: no backup, no write to `recipes.json`.

## 4. Lightbox and upgrade path

- Lightbox download changed from "fetch to Blob then save" to a plain `<a href="/cgi-bin/download?f=...">` link — the browser saves it directly and the page no longer reads a cross-origin original response.
- Two-layer lightbox loading: preview first, then the original with `fetchpriority="high"`. Closing now calls `stop` before clearing `src`, so a stale request cannot paint over a quick page flip.
- `update_nxrc.sh` pre-checks 7 album files (including `nxthumb.arm`) before copying and stops if one is missing — without it the album silently degrades to per-image transcoding (8–20 s each).
- `.gitignore` now whitelists shipped binaries (`nxthumb.arm`, `nxui.arm`, `nxui_en.arm`), so a clean clone no longer packs a tree with files missing.

## 5. New document

`docs/current/RUNTIME_ARCHITECTURE_DESIGN_2026-11-16.md` — recording-first background task management, file-completion events and a live album, and the layered design for thumbnails and storage writes. Includes the 11-16 recording-concurrency baseline: 20-image grid p50/p95 = 150/220 ms, 1024 px cold transcode 6.39 s, with `MemFree + Cached` bottoming out at 55.3 MiB.

## 6. Packages

| Package | Size | md5 | Entries |
|---|---|---|---|
| `NX-WA-2.88.0-nxwa.15.5.zip` | 10,512,073 B | `96de142d97a4530c965daa9a11071688` | 401 + manifest |
| `NX-WA-2.88.0-nxwa.15.5-EN.zip` | 10,511,276 B | `a092ac090262f1484777c788c35a98a1` | 401 + manifest |

## 7. Upgrade

Extract to the SD card root → insert the card → wait for the automatic sync to finish → reboot. The album should feel an order of magnitude faster on the first open. Individual images that still transcode simply have no usable embedded image (PNG, foreign photo or corrupt file) — that is expected.
