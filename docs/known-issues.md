# Known issues

Bugs seen on real hardware or in xemu that are not fixed yet, and the leads
we have. Add the build and date when you log one; delete it when it's fixed
(the commit message keeps the history).

## Not yet tested on hardware

- CPU clock check (2026-10-03, `xbox_clock_check` in `xbox_io.c`): one
  `[CLOCK] CPU ... MHz (nxdk says ...)` line at boot; a stock console reads
  ~733 and changes nothing. A CPU upgrade nxdk doesn't know (its table
  misses some Celerons and Tualatins) should say "timers use the measured
  clock".
- 128 MB consoles run as 64 MB (2026-10-03, `xbox_ramlock.c`, `memory.md`):
  the boot log's `[MEM] 128 MB console:` line says whether the kernel kept
  the upper 64 MB (expected with Cerbios and the XBE's 64 MB flag: "nothing
  above 64 MB is free") or the game held it ("upper 64 MB held (N KB ...)").
  `free` in `[MEM] boot` should be ~38 MB, as on a 64 MB console.
- v8-v10 ran on hardware 2026-10-03: the Options menu redesign, screenshots
  (right stick, `shotNN.bmp`), 40% dead zones (migrated from 43/30), the
  `[CLOCK] CPU` line (733.3 MHz, unchanged), Clu Clu Land D back to the
  room, 58-59 fps in town. Not tried yet: a save under another `.gci` name
  in `save/card_a` (Dolphin's `01-GAFE-DobutsunomoriP_MURA.gci`; `pc_card.c`
  built `-U_WIN32`, `patches.md`), the splash at 480i when
  `progressive = 0`.
- v5-v7 ran on hardware 2026-10-03 (NES, clock and audio confirmed; v7
  logged no audio starvation after boot). Not judged separately yet: the
  texture cache index (`PC_TEX_CACHE_INDEX`) and inline shim compares in a
  busy town, the texture pitch fix (no GPU faults since).
- Round C (2026-10-03): safe video (BACK at boot, 480i/480p/720p Output
  row), the clock fix over 20+ minutes. The FPS counter, 16-bit clear
  colours and `*_prev.log` work (v4).
- The Melee-X backport combo build (2026-10-03, `backport.md`; v1 ran on
  hardware 2026-10-03: stable, audio fine, 57-60 fps in town at 720p): native
  texture formats, texture reuse, per-draw skips, vertex cache break, strict
  GPU wait, 32 KB kicks, GPU overlap on by default, AC97 start/recovery/
  shutdown, session-long `boot.log`, `[FRAME]` / `[BEAT]` / `[PROF]`.
  Audio risk: the AC97 start order and recovery change the hardware path
  (xemu plays the APU voice, so it never ran them); a healthy boot logs one
  `[AUDIO] AC97 pump` line and no `halted`/`stuck`/`cold reset`. Any
  regression: set that change's key to 0 in `settings.ini`, or
  `console.py rollback`.

- 720p's 5 MB texture pool in the busiest rooms (museum, full houses):
  `perf.log` tex KB and `[NV2A] texture pool full` lines. 720p itself runs
  at 60 fps on hardware (beta-3, 2026-09-28).
- Direct disc image reads (`perf.md`, `dev` at `ab04d7fb`, deployed
  2026-09-29): the first title demo after a cold boot chugged at 12-15 fps
  for ~15 s until the villager walks down from the station; a later title
  visit never did. Judge it on perf.log minute 1 (`>17ms` was 622, `[PACE]`
  windows at 88 ms average) over several cold boots.
- CPU/GPU overlap (on by default since the backport): frame times in town vs `gpu_overlap = 0` (`perf.log`).
- Shop upgrade defaulting to Singleplayer on a console whose `settings.ini`
  predates the `[Xbox]` section.

## Z-fighting on the player model and the pockets glove (2026-10-03)

- User report on hardware (beta build, 480: 640x480x32 with Z24S8 per the
  console's boot.log): where the villager's shirt and trousers meet there
  seems to be z-fighting, and the glove hand in the pockets menu shows the
  same. To look into later.
- Leads: depth precision. It happens at 480 (Z24S8), so it isn't only the
  Z16 problem Melee-X fixed with a depth remap at 720p (its
  `docs/renderer.md` "Depth", `XGX_Z16_DEPTH_RATIO`); 720p may be worse. Also `ZMIN_MAX_CONTROL`, the depth
  range folded into the projection (`mat4_rows_mul`), and whether the seam's
  polygons share depth on the GameCube (Dolphin) too. Get a screenshot pair
  of 480 vs 720p.

## No sound: AC97 stuck at descriptor 0 (2026-10-03, v2)

- Hardware, v2: `[AUDIO] AC97 stuck: civ 0 lvi 6 sr 00/00` from the first
  buffer, eight restarts and three cold resets without a finished buffer
  (codec ready, status 00300100). v1, with the same audio code, played fine;
  the v1 session before it ended without a quit line (reset or power-off
  mid-game). In xemu with the AC97 path forced (`-DXBOX_AUDIO_APU=0`) v1 and
  v2 both run the engine. Melee-X saw the same state after a crash and only
  a power-off cleared it. Lead: a codec left stuck by an unclean end (IGR,
  reset button); check with a full power-off before launching. If it comes
  back after a power-off, suspect v2's controllers starting before the
  splash (USB up before the AC97) and test with `audio_fix = 0`.
- Confirmed (2026-10-03): after a full power-off v3 has sound. The stuck
  codec came from the unclean end of the session before; a reset or IGR
  doesn't clear it. The README says so (Known issues).

## Clu Clu Land D doesn't run (2026-10-03, v9)

- It's a Famicom Disk System game; the PC port feeds fixNES iNES
  cartridge images only (upstream too). v9 crashed starting it (a `malloc`
  sized from disk data failed, then `memset(NULL)`); v10 logs `[NES] not an
  iNES image` and goes back to the room. To run it: fixNES has an FDS path
  (`audio_fds.c`, disk sides), which needs a disk BIOS image and the AC
  disk format mapped onto it.

## Crash after an NES game: display list at a float (2026-10-03, v8)

- Hardware, v8: Super Mario Bros played from the room (~200-235 s), back in
  the room, a scene change at ~242 s, then 2 s into the new scene an access
  violation in `emu64::dl_G_DL`: `gsSPDisplayList` with address
  `0xc1180001` (tagged pointer `0xc1180000`, the float -9.5), read of
  `c1180003`, called from the frame's top-level list (`emu64_taskstart_r`).
  Some draw emitted a display-list call from a field that holds a float:
  a stale or misread struct. Not reproduced (needs the save and the
  route). v9 skips unmapped display lists with an `[EMU64]` line (address,
  segment, DL level) instead of faulting; the next log says which. Retry:
  NES game, quit, leave the house straight away.

## Crash on START at the title (GitHub #2, 2026-09-29)

- Report from an early beta (its log still has `[LOGO]` lines and closes
  `boot.log` at frame 120): pressing START on the title, the game stops;
  `last.log` ends at `aAL_setupAction: 4 -> 5` (the START press), `[STATE]`
  shows no GPU fault. Only `last.log` was posted.
- Not reproduced in xemu (2026-10-03, dev): first boot with no save and no
  `settings.ini` (`-DXBOX_DBG_FRESH_UDATA`), START at the title goes on to
  K.K. and the train, at 480; also with the pre-v6 clock (~2083). Next:
  the reporter's release, console (RAM, 480/720p), what's in
  `save/card_a`, and any `crash.log` / `hang.log`; and whether the current
  release does it.
- Second report (2026-10-03, beta-3 release, 128 MB, 1 GHz CPU swap, HDMI
  modchip, 480i): a new game starts, then crashes right after Rover's train
  dialog. `crash.log`: access violation, read of `d68301f8`, eip in
  `lbRTC_Sub_DD` (called from `Kabu_manager` <- `mSDI_StartInitAfter`) on a
  stack copy of the new save's Stalk Market date, which is all zero in a
  new town on the GameCube too. The eip is `xor esi, esi`, which can't
  read memory; the beta-3 release map matches the log's image range. Not a
  memory layout problem: the boot line says 131072 KB total but 38644 KB
  free, the 64 MB layout (Cerbios honours the XBE's 64 MB flag), and
  beta-3 at 128 MB under Cerbios in xemu goes through the train into town.
  Suspect the CPU swap. Done for it: the CPU clock check (`[CLOCK] CPU`,
  `xbox_clock_check`: nxdk's timer frequency falls back to 733 MHz for CPUs
  missing from its table, which ran the game clock and limiter fast) and the
  64 MB lock. Next: the reporter's `[CLOCK] CPU` line on the next release,
  and whether the crash is at the same point every time.

## Audio chugs (2026-10-03, v4)

- Hardware report, v4 at 720p: "the audio chugs". No `[AUDIO]` warnings in
  the log; the AC97 itself ran. The frames around it were CPU-bound (busy
  town 51 fps at 340 draws, NES 52 fps): the game thread never sleeps then,
  and the audio producer thread had the same priority, so it waited for the
  game's time slice while the ~70 ms ring ran dry. v5 runs the producer one
  step above the game (`audio_priority`, `xbox_audio.c`) and counts the
  silence the AC97 played for lack of samples (`[BEAT] ... audio starved N
  ms in G gaps`). One gap of ~25 ms at boot is the ring filling. If it
  still chugs with no starved ms, look at the synthesis itself
  (`pc_audio_process_frame`'s cost in `[PROF]`).

## Title text at 720p looks odd (2026-10-03, v4)

- User report: at 720p the "2001" and "2002" of the copyright line and some
  of the title menu's options look a little weird; probably there since
  720p came in. xemu at 720p (16:9 and 4:3) shows them clean, so it may be
  the console's 16-bit framebuffer or filtering. Needs a photo from the
  TV.
- Found on the way (xemu): at 16:9 (always at 720p) some 2D art is drawn
  ~1.3x too wide, the title logo for one; only the 3D is hor+ and some 2D
  is pillarboxed (`pc_gx.c`). The main game stays 16:9 (user's choice); NES
  games are 4:3 between bars (`nes_aspect`, checked at 720p in xemu with
  `XBOX_DBG_NES_TEST`).

## Choice list scrolls down on its own (2026-10-03, v3)

- Hardware report: in a conversation's choice list the cursor scrolls down
  by itself and won't go back up; walking is normal. AC turns the C-stick
  into the N64 C buttons past 29 of 127 (~23%, `contreaddata.c`) and the
  lists scroll on C-down too, while the C-stick dead zone was 12%: a worn
  right stick resting a quarter down holds C-down. v4 makes the Xbox's
  C-stick dead zone 30% (raised once for older settings files,
  `opt_version` 2); v8 makes both sticks 40% (`opt_version` 3). Confirm on
  hardware; `stick0.log` (L3) has raw reads.

## NES at 720p (2026-10-03)

- Fixed in v7 (hardware, 2026-10-03: "literally flawless", 59.4 fps, 4:3
  between bars in `nes_shot.raw`): the picture was drawn a second time,
  full screen, by `famicom.cpp`'s GameCube quad (`patches.md`). v5's
  upload fast path and v7 together took NES from 52 to ~59 fps. NES frames 17.8 ms in v5
  (56 fps; upload 1.4 ms, was 3.0).
- v4 on hardware: NES plays at 720p (heap 3072 KB, fixNES's allocations
  fit) and the save file is written. Left:
  - Free RAM during NES play is 244 KB (perf.log minute 3): one more
    allocation and something fails. Options: a smaller 720p texture pool
    while NES runs, or fixNES's noise table (508 KB) shared or shrunk.
  - Frames 19 ms (52 fps): fixNES 13.3 ms, the screen upload 3 ms (fixed
    in v5: two texels a word), gpu wait ~2 ms. fixNES's `ppuCycle` alone is
    ~24% of NES samples; per-TU `-O3` for `ppu.c`/`cpu.c`/`apu.c` is the next
    thing to try.
  - On leaving the game the log shows `ファミコン共通セーブは不正です` /
    `共通セーブ領域が壊れているのでセーブしません` (the common NES save area
    is invalid, not saved), right before `[NES] internal save write ...: ok`.
    Possibly a first play with no area yet, possibly byte order; check
    whether high scores survive a second play.

### Earlier: "memory card in slot A could not be read"

- Cause found with v3's `[NES]` lines: `famicom_init` failed on
  `MALLOC_MALLOC ... CHR_TO_I8_BUF_SIZE 1048576Byte 確保失敗`. The PC branch
  of `famicom_emu_init` mallocs a fixed 4 MB heap for the emulator, and at
  720p only ~3.8 MB is free (perf.log `free 3824 KB`); the room reports the
  failure as a card error. The beta ran NES at 480 (~5.5 MB free). v4 falls
  back to 3.5 / 3 / 2.75 MB (`[NES] emulator heap` line); on hardware it
  got 3 MB and played.
- Also fixed on the way: `famicom.cpp` (C++) never got the path routing, so
  the NES save file was never read or written on the Xbox.

## Clock: 2083, and hours off after a reboot (fixed in v6, 2026-10-03)

- v5 report: the time was set right in the game and saved; after a reboot
  the game said ~3 hours later. Cause (also the 2083 date): `OSInit`'s
  time-zone offset came from nxdk's `mktime`, which returns -1, so the
  console clock was UTC plus the boot's own Unix time (~2083), and each
  boot moved it on by the real time since the previous boot. The save's
  `time_delta` read -56.8 years. v6 takes the offset from the dashboard's
  time zone (`[CLOCK]` line at boot) and resets such deltas to 0 once
  (`[CLOCK] save's time offset ... reset`), so the game shows the
  dashboard's local time. To check on hardware: time right after boot,
  after 20+ minutes, and after a reboot. The notes below are the history.

## Date sometimes comes up as 2083 (2026-09-29)

- User report on hardware: the in-game date seems to default to 2083,
  apparently at random. Not reproduced or logged yet.
- The clock is set once at boot in `OSInit` (`pc_os.c`): `time(NULL)` from
  nxdk, turned into GameCube ticks since 2000 with a timezone offset from
  `gmtime`/`mktime`. Leads: what nxdk's `time()` returns when the console
  clock is unset or the RTC capacitor has drained, and whether its
  `mktime` fails (`-1` makes the offset huge). Log `unix_now`,
  `tz_offset_secs` and `gc_secs` at boot to catch a bad start.
- The clock not keeping time (2026-10-03 report) was `osGetTime`'s overflow,
  fixed in round C (`patches.md`): nxdk's performance counter is the TSC at
  733 MHz, and `(now - start) * 40.5 MHz` passed 2^64 10.4 minutes after
  boot, so the game clock jumped back every 10.4 minutes. Check on hardware
  that the clock keeps time over 20+ minutes; the 2083 start is separate.

## Not ported

- GameCube TEV swap tables and indirect textures in the combiner generator.
  Nothing visibly wrong in play so far.
