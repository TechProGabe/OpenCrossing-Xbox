# Known issues

Bugs seen on real hardware or in xemu that are not fixed yet, and the leads
we have. Add the build and date when you log one; delete it when it's fixed
(the commit message keeps the history).

## Not yet tested on hardware

- 720p's 5 MB texture pool in the busiest rooms (museum, full houses):
  `perf.log` tex KB and `[NV2A] texture pool full` lines. 720p itself runs
  at 60 fps on hardware (beta-3, 2026-09-28).
- Direct disc image reads (`perf.md`, `dev` at `ab04d7fb`, deployed
  2026-09-29): the first title demo after a cold boot chugged at 12-15 fps
  for ~15 s until the villager walks down from the station; a later title
  visit never did. Judge it on perf.log minute 1 (`>17ms` was 622, `[PACE]`
  windows at 88 ms average) over several cold boots.
- CPU/GPU overlap (`gpu_overlap = 1`): frame times in town vs off (`perf.log`).
- Shop upgrade defaulting to Singleplayer on a console whose `settings.ini`
  predates the `[Xbox]` section.

## Saves under any other name are never found (2026-09-29)

- A user report: a GameCube save exported with Dolphin's memory card manager
  as `01-GAFE-DobutsunomoriP_MURA.gci` in `save/card_a` didn't load. Only
  the exact names `DobutsunomoriP_MURA.gci` and
  `8P-GAFE-DobutsunomoriP_MURA.gci` work (`pc_save_scan_gci_dir`). The
  "any `.gci` in the folder" fallback, `pc_card_scan_for_gci` in
  `pc_card.c`, takes its `_WIN32` branch (nxdk defines `_WIN32`) and calls
  `FindFirstFileA("save/card_a\*.gci")` with a relative path, which never
  resolves on the Xbox. The same scan finds a visiting town in
  `save/card_b`, so that is broken too.
- Fix: build `pc_card.c` with `-U_WIN32` so it takes the `opendir` branch
  (`xbox_posix.c` resolves `save/...` to `E:\UDATA\4f430001\`); check its
  `mkdir`/`strcasecmp` fall back cleanly. Workaround until then: rename the
  file to `DobutsunomoriP_MURA.gci` (an existing file of that name wins).
- Even once fixed, `DobutsunomoriP_MURA.gci` is loaded first when it exists,
  so an imported save under another name loses to a town started on the
  Xbox. Worth a line in the README's save instructions.

## NES games run a little choppy

- Hardware, 2026-09-29 (`[NES]` lines in perf.log): fixNES alone takes
  13-14 ms per NES frame and the whole frame 17-18 ms, so most frames miss
  the 16.7 ms vblank by 1-2 ms. Better than before vblank pacing ("improved,
  not 100%"). The ~4 ms outside the emulator is the lead: the per-frame
  RGB565 to swizzled A8R8G8B8 conversion of a padded 256x256 texture into
  a fresh pool block (`tex_image_2d` in `xbox_nv2a.c`), then the draw and
  present. Options: upload in place when the size doesn't change, or keep
  RGB565 (build fixNES without `COL_TEX_BSWAP` so red is in the high bits).
  The emulator itself steps CPU, PPU, APU and mapper through separate calls
  every cycle (`pc_fixnes_frame`).

## Not ported

- GameCube TEV swap tables and indirect textures in the combiner generator.
  Nothing visibly wrong in play so far.
