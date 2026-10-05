# Known issues

Bugs seen on real hardware or in xemu that are not fixed yet, and the leads
we have. Add the build and date when you log one; delete it when it's fixed
(the commit message keeps the history).

## Not yet tested on hardware

As of v14 (2026-10-03). Everything else ran on the playtest consoles:
"red" (64 MB, 733 MHz, component cable, 720p) through v11, and "gold"
(64 MB, composite cable, 480i only) from v13. Restart from the Options menu
was checked in xemu (it relaunches into the game at the new output).

- A save under another `.gci` name in `save/card_a` (Dolphin's
  `01-GAFE-DobutsunomoriP_MURA.gci`; `pc_card.c` built `-U_WIN32`,
  `patches.md`).
- Visiting another town (a second town's `.gci` in `save/card_b`): the
  scan never worked on the Xbox before `pc_card.c -U_WIN32`, so the trip
  has never run on one. A failed trip now cancels before home is touched,
  and with no other town the Porter says there's no town data (v13 on
  hardware took the GameCube's passport trip instead: back to the title;
  `patches.md`). Check: no `card_b`, a copy of your own town there (refused
  as the same town), another town (the trip, and the way back).
- The splash and the no-disc screen at 480i when `progressive = 0`
  (`xbox_settings_early`).
- Video auto-detection (`XBOX_VIDEO_AUTO`, `opt_version` 4; xemu only so
  far, every dashboard combination): a fresh install and the playtest
  console's old `settings.ini` should come up at 720p 16:9 there, and the
  `[VIDEO]` lines in `boot.log` should match the dashboard. Still to see on
  hardware: 480p and 480i dashboards, a 4:3 dashboard at 480 (4:3), a
  composite cable (480i), safe video (BACK at boot) after the move, and
  whether 16:9 by default at 480 costs too much frame time in busy rooms
  (the old 4:3 default's reason).
- A 128 MB console (`xbox_ramlock.c`, `memory.md`): the boot log's
  `[MEM] 128 MB console:` line says whether the kernel kept the upper 64 MB
  (expected with Cerbios and the XBE's 64 MB flag) or the game held it.
  `free` in `[MEM] boot` should be ~38 MB, as on a 64 MB console.
- A CPU upgrade (`xbox_clock_check`): the `[CLOCK] CPU ... MHz` line; one
  nxdk's table doesn't know (some Celerons and Tualatins) should say
  "timers use the measured clock". Users' 1 GHz and 1.4 GHz consoles
  measured right (GitHub #3); the 1.4 GHz one plays with v1.1's code
  repair (`traps.md`, the rdtsc patcher).
- The z-fighting fix (shirt hem over the trousers, the pockets glove):
  `frame_open` turns pbkit's w-buffer back off every frame (`renderer.md`
  "Depth", `XBOX_ZBUFFER`). Fixed in xemu at 720p and 480 against the v11
  hardware shots; on the console, check the hem, the glove, shadows, water,
  snow and the house interiors at both outputs, and that nothing near the
  camera is newly cut off (depth is clipped at the near plane now, as on
  the GameCube). The boot log's `[NV2A] ... z-buffer 1`.
- Judged only as part of a whole build, never one at a time: the texture
  cache index (`PC_TEX_CACHE_INDEX`), CPU/GPU overlap vs `gpu_overlap = 0`,
  direct disc image reads on the first title demo after a cold boot
  (`perf.md`), 720p's 5 MB texture pool in the busiest rooms (museum, full
  houses: `[NV2A] texture pool full`). Any regression from the Melee-X
  backport: set that change's key to 0 in `settings.ini` (`backport.md`),
  or `console.py rollback`.

## Crash after an NES game: display list at a float (2026-10-03, v8)

- Hardware, v8: Super Mario Bros played from the room, back in the room, a
  scene change, then 2 s into the new scene an access violation in
  `emu64::dl_G_DL`: `gsSPDisplayList` with address `0xc1180001` (tagged
  pointer `0xc1180000`, the float -9.5), from the frame's top-level list
  (`emu64_taskstart_r`). Some draw emitted a display-list call from a field
  that holds a float: a stale or misread struct.
- Not reproduced (v9 and v10 played NES games and left the room without
  it). Since v9 an unmapped display list is skipped with an `[EMU64]` line
  (address, segment, DL level) instead of faulting: that line names the
  culprit if it comes back.

## Title text at 720p looks odd (2026-10-03, v4)

- User report: at 720p the "2001" and "2002" of the copyright line and some
  of the title menu's options look a little weird. The beta-3 vs release
  comparison (xemu, 2026-10-03, `~/xemu/regress/compare/s2-*`) found two
  old causes, the same in both builds: at 720p the copyright digits come
  out unevenly sized (the 2D art's non-integer upscale), and the title
  menu items are drawn under the 3D scene: the player's head covers
  "Start Game" and the cherry trees "Options" / "Quit Game", which are dark
  translucent brown and hard to read on foliage (at 480 too). Leads: draw
  the menu after the scene (`ac_animal_logo.c`, `PC_ENHANCEMENTS` menu) and
  snap 2D scaling to whole pixels at 720p.
- Related (xemu): at 16:9 (always at 720p) some 2D art is drawn ~1.3x too
  wide, the title logo for one; only the 3D is hor+ and some 2D is
  pillarboxed (`pc_gx.c`). The main game stays 16:9 (user's choice); NES
  games are 4:3 between bars.

## Smaller things from the beta-3 comparison (2026-10-03)

- Options > Controls > Buttons: rows are tight, glyphs sit on the ruled
  lines and the "A" pokes out of the bottom of the highlight bar.
- xemu town walks log ~900 `[HITCH]` frames of 40-59 ms per run (beta-3:
  49-165) at the same or better average fps: vblank pacing or GPU overlap
  rounding frame times on xemu's slow GPU. Judge on hardware (perf.log
  `>33ms`) before calling it real.

## NES leftovers

- Clu Clu Land D doesn't run: it's a Famicom Disk System image, and the PC
  port feeds fixNES iNES cartridges only (upstream too). Since v10 the game
  logs `[NES] not an iNES image` and goes back to the room (it crashed
  before). To run it: fixNES's FDS path (`audio_fds.c`, disk sides) needs a
  disk BIOS image and the AC disk format mapped onto it.
- Free RAM during NES play is low: 436 KB at 720p (red, v10) and 316 KB at
  480i (gold, v13, `perf.log` minute 2; the 4 MB heap fits at 480, so less
  is left over). v14 gave back 160 KB. One large allocation and something
  fails. Options: a smaller 720p texture pool
  while NES runs, or fixNES's noise table (508 KB) shared or shrunk.
- On leaving a game the log shows `ファミコン共通セーブは不正です` /
  `共通セーブ領域が壊れているのでセーブしません` (the common NES save area
  is invalid, not saved), right before `[NES] internal save write ...: ok`.
  Check whether high scores survive a second play.

## No sound after an unclean end

- `[AUDIO] AC97 stuck: civ 0 ...` from the first buffer after a session
  ended with a reset or IGR mid-game (v2, 2026-10-03); a full power-off
  clears it (confirmed with v3). Melee-X sees the same. The README tells
  players to power off. A fix would need a codec reset the AC97 accepts in
  that state; three cold resets didn't do it.

## Not ported

- GameCube TEV swap tables and indirect textures in the combiner generator.
  Nothing visibly wrong in play so far.
