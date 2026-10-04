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
  "timers use the measured clock".
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

## Crash in `lbRTC_Sub_DD` on an upgraded console (GitHub #2, #3)

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
  128 MB console runs fine. Suspect the CPU swap.
- v1, same console (GitHub #3, 720p now): the same fault twice in a row,
  this time on START at the title (`mTM_time` -> `Kabu_manager` on the day
  change, frames 511 and 624). `[CLOCK] CPU 1000.0 MHz (nxdk says 999.9)`,
  cpuid `00000686` (Coppermine cC0; the stock CPU is `068a`), the 64 MB
  lock held (`free 38412 KB`). So not the clock, not the memory, and not a
  flaky part: it is the same instruction and the same address every time,
  in two builds that put the function at different addresses.
- The fault address is the code itself: the bytes at the eip are
  `31 f6 | 83 f8 01 | 83 d6 0b` (`xor esi, esi; cmp eax, 1; adc esi, 11`),
  and `d68301f8` is bytes 3-6 of that. Skip the `31` and the rest decodes
  as `f6 83 f8 01 83 d6 0b` = `test byte [ebx + 0xd68301f8], 0x0b`, with
  ebx = 0: exactly the read that faulted. The registers are those of a
  correct run up to the eip (the zero date takes the month-0 path, which
  every new town takes on every console). Either that CPU decodes this
  sequence one byte off, or the byte at the eip isn't `31` in that
  console's RAM (a BIOS or modchip patcher matching a signature; a bad RAM
  cell is ruled out, no prefix is one bit from `31` and the address moved
  between builds).
- Verified against the release XBEs (2026-10-04): beta-3 puts
  `lbRTC_Sub_DD` at `0032c830` and #2's eip `0032c90c` is the same offset
  `+dc` as v1's `0033413c` (`00334060`); both files hold the identical
  `31 f6 83 f8 01 83 d6 0b` there, and both logs show the same registers
  (`eax ffffffff ebx 0 esi d ebp 1`, only `ecx` differs: the garbage
  `lbRTC_GetDaysByMonth(year 0, month 255)` returns, 57 vs 9). The
  `xor esi, esi` sits at offset `c` of a 16-byte fetch window in both
  builds (the function is 16-aligned), so `cmp eax, 1` straddles the
  window in both; the 32-byte line alignment differs (`00` vs `10` mod
  32). It is reached by fallthrough from a not-taken `jne`; the January
  path reaches it by `jmp`. The 8-byte sequence is unique in the binary;
  the other `xor esi,esi; cmp eax,1` sites are followed by a `jcc` and
  none sits at offset `c`. Stepping 0686 is cC0, older than the stock
  cD0 (068a); an Xbox BIOS carries no microcode for it, if it carries any.
  No Intel erratum was matched (the spec update PDF didn't extract).
- Troubleshooting build for the reporter (`-DXBOX_DIAG_ISSUE3=ON`,
  `toolchain.md`; `xbox/src/xbox_diag.c`): `boot.log` gets `[DIAG]` lines
  (cpuid, microcode revision from MSR 8Bh, P6 MSRs, CR0/CR4, kernel
  version; `.text`/`.rdata` in RAM compared with `D:\default.xbe`;
  `lbRTC_Sub_DD` on a zero date under an exception guard, from its real
  address with `wbinvd` and 1000 plain runs, the January `jmp` path, and
  copies at all 64 alignments), and `crash.log` gets the code bytes at the
  eip from RAM and from the disk (`[CRASH] ram eip+0:` / `disk eip+0:` /
  `ram vs disk:`). `settings.ini` `rtc_shim = 1` routes `lbRTC_Sub_DD`
  through a plain -O0 copy of its C (`XBOX_RTC_SHIM`, `patches.md`).
  Checked in xemu (2026-10-04): `.text`/`.rdata` match the disk, the guard
  catches a deliberate read of `d68301f8` and boot goes on (`[DIAG] guard
  self-test`), every rtc run passes, and a forced crash (`XBOX_DBG_CRASH_FRAME`)
  logs the ram/disk code lines with `ram vs disk: identical`. On red
  (stock cD0, kernel 5101, microcode revision 1): the same, all 64 copies
  ok, then title, START and town as usual.
  Reading the result: `ram vs disk: identical` + a `[DIAG] rtc ... FAULT`
  at boot = the CPU (and the copies say whether alignment matters; if the
  shim then runs through, ship it as a `cpuid 0686` workaround); `RAM !=
  disk` or `differ at eip+0` = something patches the image in RAM (BIOS,
  modchip, kernel patcher: ask for the BIOS name/version); all boot tests
  ok but the title crash stays = the fault needs the game's context
  (cache/timing state), still the CPU side. Still wanted from the reporter:
  BIOS name and version, the same build on their 1.4 GHz console.
- An earlier report (early beta, crash on START at the title, `last.log`
  only) wasn't reproduced either; likely the same fault.

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
