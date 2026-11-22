# Metol 1.15.7

Released: 2026-11-20

Two things: **V-Log tables no longer get their own tab**, and a pipeline that bakes third-party
V-Log creative LUTs into tables the camera can use.

## 1. Tap the LUT tab again to flip domain

The top bar has exactly two tabs: `● LUT` and `FILMLAB`. V-Log tables do not get a third one —
they write the same A18 slot as normal tables; only the input domain differs.

- From FilmLab, tapping LUT returns to the LUT layer (domain unchanged)
- **Already on LUT, tapping LUT flips the domain**: standard ⇄ V-Log, the tab becomes `● LUT·VLOG`
- Flipping only reloads one list — no A18 write, no DMA; each domain keeps its own selection

Standard tables live in `looks/`, V-Log tables in `looks/_vlog/`.

Also: **`EV + AEL` now opens the V-Log curve page**. V-Log sits three levels deep in the menu
(double-tap EV → Custom Func. → V-Log Curve) and it is one of the most frequently flipped switches
when shooting video; the slot has been free since FilmLab moved into the panel. Same semantics as
EV+WiFi: opens if closed, closes if already running.

## 2. Lumix V-Log library

57 tables from [`changyun233/Lumix-V-log-LUTs`](https://github.com/changyun233/Lumix-V-log-LUTs):
`varicam_official` 35 + `lumix_color_lab` 22.

Upstream tables expect V-Log encoded input (18% grey = 0.42); the A18 slot sits on the camera's
JPEG domain. Writing them straight in is a double mismatch — the table assumes dark log footage
while the camera feeds it normal JPEG — so they come out under-exposed and washed out. They are
pre-composited first: `T_std(x) = T_vlog(S2V(x))`, then converted to a 17³ slot table.

Dedup is ΔE00 (threshold 3.112) against the whole 125-table library: **56/57 usable** in the
standard domain (only `lcl_cinetone` rejected, too close to `kodak_ektar100`), **57/57** in V-Log.

Baked results read **slightly flatter** than native tables (0.18 input goes 0.085 → 0.244). That
is the shadow lift of V-Log tables carried over, not a defect.

## 3. Two gotchas

- **Names are capped at 12 characters.** The row is 5 × 133 px and clips from the 13th
  (`lcl_nostbleach` rendered as `lcl_nostblea`); it is now `lcl_bleach`.
- **The V-Log domain is not always usable.** The curve engine lives in `$D/gamma/` and is not
  bundled; without it a V-Log table is a double mismatch — use the standard domain.

## 4. Fixed along the way

- `services_visible()` still used the 2×3-era six-per-page window — service toggles past item 7
  never refreshed their state.
- The verification script asserted a 336 px menu panel (actually 292), failing the suite at step one.
- Six test files hard-coded the host `test_input`, so arm click tests had never actually run.
- `load_menu()` failures now log `MENU FAIL` instead of being silent.

## 5. Credits

- **[NoireSchwarz/NX500_SRW_reprocessing_6.5K-oversampling](https://github.com/NoireSchwarz/NX500_SRW_reprocessing_6.5K-oversampling)**
  — research and fitting of the Panasonic V-Log ⇄ Standard curves. Without that curve, all 107
  V-Log tables would be double-mismatched grey on the NX500, and section 2 would not exist.
- **[changyun233/Lumix-V-log-LUTs](https://github.com/changyun233/Lumix-V-log-LUTs)**
  — curation and previews (Booz / Changyun / Hamburger / Lucas Hu / Lumix Color Lab / VariCam
  official). **These tables are not bundled**; get them from the original link.
- **[ottokiksmaler/nx500_nx1_modding](https://github.com/ottokiksmaler/nx500_nx1_modding)**
  — upstream bloodline.
