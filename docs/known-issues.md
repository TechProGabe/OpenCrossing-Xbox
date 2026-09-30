# Known issues

Bugs seen on real hardware or in xemu that are not fixed yet, and the leads
we have. Add the build and date when you log one; delete it when it's fixed
(the commit message keeps the history).

## Town not as smooth (hardware, 2026-09-28)

- Town "runs ok but not as butter", ~45-50 fps by eye. The console was
  still running the build before the settings menu (no `[Xbox]` section in
  its `settings.ini`), so the settings-menu changes are not the cause.
  That run's `perf.log`: 56-60 fps average per minute, but 5-12 frames a
  minute over 33 ms and 1-5 over 100 ms (worst ~270 ms), and free RAM down
  to 456 KB once. The hitches, not the average, are what reads as
  not smooth; `[HITCH]` lines say whether they are texture loads or GPU.
  `borderless_acres = 1` (the PC default) draws neighbouring acres too.

## Not yet tested on hardware (2026-09-28)

- 720p's 5 MB texture pool in the busiest rooms (museum, full houses):
  `perf.log` tex KB and `[NV2A] texture pool full` lines. 720p itself runs
  at 60 fps on hardware (beta-3, 2026-09-28).
- Direct disc image reads (`perf.md`): the first title demo after a cold
  boot, which chugged at 12-15 fps for ~15 s until the villager walks down
  from the station (vblank pacing alone didn't fix it; a later title visit
  never chugged). perf.log minute 1 `>17ms` and `[PACE]` lines.
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

## NES games run choppy

- Real hardware, `dev` at `f3b76f8d`, 2026-09-28: playing an NES game from
  the furniture item, the frame rate is very low and the picture is choppy.
  Not investigated yet. Leads: fixNES emulation cost on the 733 MHz CPU
  (`pc_nes_fixnes.c`), the per-frame RGB565 → swizzled A8R8G8B8 upload of the
  256×224 NES frame into a fresh texture (`tex_image_2d` in `xbox_nv2a.c`),
  and the present waiting for GPU idle. Better with vblank pacing, but
  measured (hardware, `[NES]` lines): fixNES alone takes 13-14 ms per NES
  frame and the whole frame 17-18 ms, so it misses the vblank by 1-2 ms.
  Next: cut the ~4 ms outside the emulator (RGB565 to swizzled A8R8G8B8
  upload of a padded 256x256 texture every frame) or the emulator itself.

## Not ported

- GameCube TEV swap tables and indirect textures in the combiner generator.
  Nothing visibly wrong in play so far.
