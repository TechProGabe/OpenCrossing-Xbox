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
- Vblank pacing (`perf.md`): the first title demo after boot (choppy in
  about 1 boot in 3 until the villager walks down from the station) and
  town smoothness. `[PACE]` lines in `last.log` if it still chops.
- Town and player erase at player select (`patches.md`): "clear village
  data" should lead to a new town on the next start.
- CPU/GPU overlap (`gpu_overlap = 1`): frame times in town vs off (`perf.log`).
- Shop upgrade defaulting to Singleplayer on a console whose `settings.ini`
  predates the `[Xbox]` section.

## NES games run choppy

- Real hardware, `dev` at `f3b76f8d`, 2026-09-28: playing an NES game from
  the furniture item, the frame rate is very low and the picture is choppy.
  Not investigated yet. Leads: fixNES emulation cost on the 733 MHz CPU
  (`pc_nes_fixnes.c`), the per-frame RGB565 → swizzled A8R8G8B8 upload of the
  256×224 NES frame into a fresh texture (`tex_image_2d` in `xbox_nv2a.c`),
  and the present waiting for GPU idle. `[NES]` lines (in perf.log, every
  5 s of play) give the emulator's own ms per NES frame: well under 16 ms
  means the upload/draw/present side. NES play now paces to the vblank too.

## Not ported

- GameCube TEV swap tables and indirect textures in the combiner generator.
  Nothing visibly wrong in play so far.
