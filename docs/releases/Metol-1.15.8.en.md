# Metol 1.15.8

Released: 2026-11-22

One main thing this round: **FilmLab goes from tapping a recipe to a changed picture in 510 ms, down from 790 ms**.

## 1. Preview is 35% faster

The old breakdown was: confirm the live slot 380 ms + send the seven dimensions 400 ms.

Inside that 400 ms the actual MCB writes account for **0.4 ms** — eight
`SetVariableDataMCB` calls issued back to back in one process. The rest was loading an
820 KB (-O0) binary (~60 ms), `dlopen` of `libcapture-fw-prod` (73 ms), a fixed 7×20 ms
gap between calls (142 ms), and a 100 ms tail sleep.

The last two are A1-era conservative values, and both turned out to be unnecessary:

- With the gaps removed, a full seven-dimension change (112/111/108 12/8/9/10) reads back
  through `varlist` as R/G/B=111/111/107 HUE=12 SAT=8 SHARP=9 CON=10 — every dimension
  landed, the trailing commit bit fired, and prefman slot 9 was never written. Sending the
  original values back reproduces the baseline dimension by dimension.
- The 100 ms tail existed to give a "read back" room. Preview exits right after sending;
  read-back is done separately by `filmlab.sh check`.

The new `pwfast` is a 3.3 KB `-nostdlib` binary that needs only `libdl.so.2` and completes
in 68 ms. `filmlab.sh` prefers it and falls back verbatim to the old `pwsend seq` if the
file is missing.

## 2. A latent bug that could corrupt a custom slot

The previous release cached the slot check for 90 seconds to save one read. The stamp
recorded only a timestamp, not *which slot you were on*: preview while on CUSTOM1, spin the
physical dial to CUSTOM2, preview again within 90 s, and the commit bit wrote the seven
dimensions into CUSTOM2 — while the script had only ever backed up slot 9, leaving that
slot unrecoverable.

The cache is withdrawn; every preview reads the live slot. That brings the 380 ms back, and
buys the guarantee that no slot can be written by accident.

## 3. Album

- **Thumbnail prewarm is off by default.** NX500 originals already carry a 320 px (IFD1)
  and 1616 px (MPF) embedded image, so slicing those bytes is enough. Prewarming 62 photos
  reported `generated=0 sliced=62` — pure busywork that still burned single-core CPU and SD
  reads. It can be started manually when importing foreign photos or PNGs.
- **Concurrent requests no longer trip over each other**: in-flight requests use a ticket
  table, directory switches carry a generation number so late callbacks are dropped,
  over-concurrency queues instead of discarding, and collapsing the panel cancels queued
  requests — no more "opens empty, then content shows up".

## 4. Smaller fixes

- `capdtm-httpd stop` used to run `killall busybox` first, which takes down everything with
  `comm=busybox` (telnetd included) before the precise filter ran. That first line was
  redundant and self-defeating; removed.
- LUT table swaps gained `NX_TIMING` section timing (zero cost when off), used to decide
  whether first-enable and subsequent swaps can be split apart.

## Known limitation

380 of the remaining 510 ms is a single `st cap capdtm getusr 20`. It is slow not because of
reading the slot but because `st` links 83 shared libraries — `time` shows user 0.29 s /
sys 0.02 s, pure CPU loading. Bypassing it means reversing CAP DTM itself; three
approaches (System V message queue / prefman keys / whole-disk string search) were all
disproven this round, so it will not be attempted again for now.
