# Known issues

Bugs seen on real hardware or in xemu that are not fixed yet, and the leads
we have. Add the build and date when you log one; delete it when it's fixed
(the commit message keeps the history).

## Not yet tested on hardware

As of `c7a0da51` (v10, 2026-10-03). Everything else in that build ran on
the playtest console (64 MB, 733 MHz, 720p).

- A save under another `.gci` name in `save/card_a` (Dolphin's
  `01-GAFE-DobutsunomoriP_MURA.gci`; `pc_card.c` built `-U_WIN32`,
  `patches.md`).
- The splash and the no-disc screen at 480i when `progressive = 0`
  (`xbox_settings_early`).
- A 128 MB console (`xbox_ramlock.c`, `memory.md`): the boot log's
  `[MEM] 128 MB console:` line says whether the kernel kept the upper 64 MB
  (expected with Cerbios and the XBE's 64 MB flag) or the game held it.
  `free` in `[MEM] boot` should be ~38 MB, as on a 64 MB console.
- A CPU upgrade (`xbox_clock_check`): the `[CLOCK] CPU ... MHz` line; one
  nxdk's table doesn't know (some Celerons and Tualatins) should say
  "timers use the measured clock".
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

## Crash after Rover's train on an upgraded console (GitHub #2)

- Beta-3 release, 128 MB, 1 GHz CPU swap, HDMI modchip, 480i: a new game
  starts, then crashes right after the train dialog. `crash.log`: access
  violation (read of `d68301f8`) with eip in `lbRTC_Sub_DD` (from
  `Kabu_manager` <- `mSDI_StartInitAfter`), on a stack copy of the new
  save's Stalk Market date, which is all zero in a new town on the
  GameCube too. The eip is `xor esi, esi`, which can't read memory, and the
  release map matches the log's image range.
- Not the memory layout: the log says 131072 KB total but 38644 KB free
  (Cerbios honours the XBE's 64 MB flag), and beta-3 at 128 MB under
  Cerbios in xemu goes through the train into town; another player's
  128 MB console runs fine. Suspect the CPU swap. Since then: the CPU clock
  check and the 64 MB lock. Next: the reporter's `[CLOCK] CPU` line on the
  next release, and whether it crashes at the same point every time.
- An earlier report (early beta, crash on START at the title, `last.log`
  only) wasn't reproduced either.

## Z-fighting on the player model and the pockets glove (2026-10-03)

- User report on hardware (480: Z24S8): where the villager's shirt and
  trousers meet, and on the glove hand in the pockets menu.
- Leads: depth precision. It happens at 480 (Z24S8), so it isn't only the
  Z16 problem Melee-X fixed with a depth remap at 720p (its
  `docs/renderer.md` "Depth", `XGX_Z16_DEPTH_RATIO`); 720p may be worse.
  Also `ZMIN_MAX_CONTROL`, the depth range folded into the projection
  (`mat4_rows_mul`), and whether the seam's polygons share depth on the
  GameCube (Dolphin) too. Get a screenshot pair of 480 vs 720p.

## Title text at 720p looks odd (2026-10-03, v4)

- User report: at 720p the "2001" and "2002" of the copyright line and some
  of the title menu's options look a little weird. xemu at 720p shows them
  clean, so it may be the console's 16-bit framebuffer or filtering; a
  screenshot from the console (screenshots setting) settles which.
- Related (xemu): at 16:9 (always at 720p) some 2D art is drawn ~1.3x too
  wide, the title logo for one; only the 3D is hor+ and some 2D is
  pillarboxed (`pc_gx.c`). The main game stays 16:9 (user's choice); NES
  games are 4:3 between bars.

## NES leftovers

- Clu Clu Land D doesn't run: it's a Famicom Disk System image, and the PC
  port feeds fixNES iNES cartridges only (upstream too). Since v10 the game
  logs `[NES] not an iNES image` and goes back to the room (it crashed
  before). To run it: fixNES's FDS path (`audio_fds.c`, disk sides) needs a
  disk BIOS image and the AC disk format mapped onto it.
- Free RAM during NES play at 720p is 436 KB (`[BEAT]`): one large
  allocation and something fails. Options: a smaller 720p texture pool
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
