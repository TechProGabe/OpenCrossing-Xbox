# Architecture

How the port is put together and why. Subsystem detail lives in
`renderer.md`, `memory.md` and `perf.md`; gotchas in `traps.md`.

## Base

- Game code: the ACreTeam decompilation (`src/`, `include/`), merged at
  decomp head `09ca8e8b`.
- Platform layer: flyngmt/ACGC-PC-Port v0.9.3 (`pc/`), used unmodified except
  for the fixes in `patches.md`.
- Xbox layer: `xbox/`, built with nxdk (`toolchain.md`).
- Target: retail Xbox with the stock 64 MB. 128 MB is never required.

The Xbox is a 32-bit little-endian x86 machine, the same ABI the PC port
already targets. Game logic, emu64, culling, texture decoding, the disc
reader and the save code compile unchanged. Only the platform seams are new:
video, audio, input, memory, file paths and the TEV-to-combiner translation.

## Hardware compared

| | GameCube | PC port | Xbox |
|---|---|---|---|
| CPU | Gekko PPC 485 MHz | x86-32 | Pentium III-class 733 MHz, 128 KB L2, SSE1 |
| RAM | 24 MB + 16 MB ARAM | plenty | 64 MB shared by CPU and GPU |
| GPU | Flipper, TEV (≤16 stages) | GL 3.3 shaders | NV2A: vertex programs, 4 textures per pass, 8 general + final register combiners |
| Audio | DSP + ARAM | SDL + rspsim | AC97 (MCPX APU under xemu) |
| Storage | 1.46 GB disc | file | HDD (FATX) or DVD/CD at `D:\` |
| Save | memory card | GCI file | GCI file on the HDD |

A TEV stage (`d + (1-c)·a + c·b`, bias, scale) maps onto one or two NV2A
combiner stages (`A·B + C·D` with input mappings). The game uses at most 3
TEV stages, well under the 8 combiner stages.

## Frame path

```
disc image (.iso/.gcm/.ciso) -> pc_disc.c (FST, Yaz0, DOL/REL assets)
decomp C -> N64 display lists -> emu64 -> GX calls
  -> pc_gx*.c (batching, state dedup, AABB cull, texture decode + cache)
  -> GL 3.3 subset -> xbox_nv2a.c (GL shim over pbkit) -> NV2A
```

`pc_gx.c` loads GL through glad; `xbox_gl_nv2a_load()` fills the glad
pointers with the shim, so `pc/` needs no Xbox branches. Only `pc_gx_tev.c`
(GLSL) is swapped for `xbox_gx_tev.c`.

## Platform seams (`xbox/src/`)

| file | job |
|---|---|
| `xbox_main.c` | entry, finds the disc image, controllers, input events (pause, screenshots), frame counter |
| `xbox_splash.c` | the boot splash, progress bar and error cards |
| `xbox_io.c` | path mapping, logging, file flushing, `boot.log`, the disc image's reads (around pdclib), the CPU clock check |
| `xbox_posix.c` | the POSIX file calls `pc/` uses (`opendir`, `stat`, `mkdir`, `strcasecmp`) on nxdk's Win32 subset |
| `xbox_nv2a.c`, `xbox_tev_rc.c`, `xbox_gx_tev.c`, `shaders/gx.vsh` | renderer (`renderer.md`) |
| `xbox_fbdump.c` | screenshots: BMP files on the console, COM1 dumps in xemu |
| `xbox_aram.c` | sparse ARAM with disc-backed regions (`memory.md`) |
| `xbox_audio.c` | own polled AC97 driver on hardware, APU voice under xemu |
| `xbox_pad_axis.c` | left-stick shaping for worn controllers, stick trace, rumble scaling |
| `xbox_settings.c` | `[Xbox]` section of `settings.ini`, video mode choice, logical screen size, quit / restart |
| `xbox_settings_menu.c` | the Options page (title screen and pause menu), replaces `pc_settings_menu.c` (Options menu, below) |
| `xbox_watchdog.c` | hang reporter (screen + `hang.log`), rolling `last.log` |
| `xbox_crash.c` | CPU exception reporter (screen + `crash.log`) |
| `xbox_mem.c` | word-at-a-time `mem*` (pdclib's are byte loops); the prelude makes constant-size calls builtins |
| `xbox_ramlock.c` | a 128 MB console runs as 64 MB: holds the free pages above 64 MB at boot (`memory.md`) |
| `xbox_prof.c` | sampling profiler of the game thread (`-DXBOX_PROF=1`, `perf.md`) |
| `xbox_autopad.c` | scripted pad input for xemu tests (`-DXBOX_AUTOPAD`, never in releases) |

## Files on the console

The XBE's own directory is mounted as `D:\`, whether it runs from the HDD
or from a disc. `xbox_main.c` scans it for the first `.iso`, `.gcm` or
`.ciso` whose header says GAFE01, so the image can have any name.

Everything written goes to `E:\UDATA\4f430001\` in every launch mode (a disc
is read-only): `settings.ini`, `keybindings.ini`, `save/card_a/*.gci` and
the logs (`boot.log`, `last.log`, `crash.log`, `hang.log`, `perf.log`,
`input.log`, `stickN.log`; the previous boot's as `*_prev.log`) and
screenshots (`shotNN.bmp`). Saves use the GameCube `.gci` format, so they
move between this port, Dolphin, the PC port and the other OpenCrossing
ports.

The home town is `save/card_a/DobutsunomoriP_MURA.gci` (also accepted:
`8P-GAFE-DobutsunomoriP_MURA.gci`). That file wins when it exists;
otherwise the first `.gci` in the folder whose header says GAF loads
(Dolphin exports `01-GAFE-...`; `pc_card.c`, `patches.md`). `save/card_b` is scanned the same way for a
town to visit. Each save rotates the previous file to `.bak1`..`.bak3`, which are only read if the main file can't be; "clear
village data" writes the town with its save check cleared, so its previous
state is `.bak1`.

Saves are flushed to disk on close, and the volume is flushed on rename.
FATX caches directory entries, and Mr. Resetti's "quit without saving" check
is a save written at load time, so an unflushed save would get the player
lectured after a clean power-off.

`settings.ini` is the PC port's file plus an `[Xbox]` section. The PC
writer rewrites the whole file, so `xbox_settings.c` appends the section
after every save. Its keys:

| key | default | meaning |
|---|---|---|
| `xbox_stick_deadzone` | 40 | left stick radial dead zone, % (the C-stick's is the PC port's `cstick_deadzone`, also 40 on the Xbox) |
| `rumble` | 100 | rumble strength, % |
| `video_720p` | 1 | 1 = 720p where the dashboard allows it, 0 = stay at 480 |
| `progressive` | 1 | 1 = 480p where the dashboard allows it, 0 = 480i |
| `widescreen` | 2 | 0 = 4:3, 1 = 16:9, 2 = Auto (the dashboard's setting) |
| `fps_counter` | 0 | FPS counter |
| `screenshots` | 0 | right stick click saves `shotNN.bmp` |
| `gpu_overlap`, `native_textures`, `texture_reuse`, `draw_skip`, `vertex_cache_break`, `strict_gpu_wait`, `pushbuffer_kick_kb`, `audio_fix`, `audio_priority` | 1 (32 KB) | menu-less test switches for the Melee-X backport: 1 = new behaviour, 0 = the old one, read at boot (`renderer.md`, Kill switches) |
| `opt_version` | 4 | one-time moves for files written by older builds: 2 raised the C-stick dead zone to 30%, 3 moved both sticks' old defaults (43% and 30%) to 40%, 4 moved the old video defaults (480, 4:3) to Auto |

The first playtest builds kept the left stick dead zone in
`controller.ini`; that file is no longer read.

## Visiting another town

The station's train to another town reads the other town from
`save/card_b` (any `.gci` whose header says GAF). With no other town there,
or a copy of the player's own, the Porter says there's no town data: the
GameCube's passport-only trip (save the traveller to the card in slot B
and end the game) has nothing to write to here. The trip loads the other
town before it saves home with the player marked away, so a town that
can't be read cancels the trip with home untouched (`patches.md`). It
hasn't run on a console yet (`known-issues.md`).

## Options menu

The title screen shows upstream's Start Game / Options / Quit Game menu
(`ac_animal_logo.c`), and Back opens the pause menu (Resume / Settings /
Quit Game) in game. Both drive the Options page in `xbox_settings_menu.c`:

| tab | rows |
|---|---|
| Video | Output (Auto, the default, shown with the mode it gives, e.g. "Auto, 720p"; or 480i, or 480p where the dashboard allows it, pinned; needs a restart), Widescreen (Auto, the default, shown as "Auto, 16:9" or "Auto, 4:3"; 4:3; 16:9, which draws more of the scene and costs frame time), Texture filter, FPS counter (live), Screenshots (R-Stick; live) |
| Audio | Master volume |
| Controls | Stick deadzone (radial, 0-60%, 40% default, live stick meter), C-stick deadzone (40% default), Rumble (0-100%), Buttons (controller rebinding) |
| Gameplay | Resetti, Shop upgrade (Singleplayer by default on the Xbox: the visitor Nookington's wants needs a second town in `save/card_b`; switched once on the first boot without an `[Xbox]` section), Borderless acres, NES aspect |

The page is drawn in the game's own style (cream notebook sheet in a wood
frame, a green name tag, speech-bubble prompts, the selected row on a
yellow band with value arrows, unapplied values in orange) from untextured
triangles in the font display list: no textures, allocations or file I/O.
Changes wait for Apply; leaving with unapplied changes asks first. One
`[MENU]` line per visit logs the display lists' headroom. The pause menu's
own Resume / Settings / Quit page (`pc_pause_menu.c`) keeps the PC port's
look.

Quit Game goes back to the dashboard (`XLaunchXBE(NULL)`; nxdk's `exit`
reboots, which relaunches a disc). Applying a new output offers a restart
(`XLaunchXBE` of the kernel's own path for this XBE, `XeImageFileName`:
`\Device\Harddisk0\Partition6\...`; `traps.md`). Both first stop the
sound and the USB host controller (`leave_game`, `traps.md`).

## Video output

As Melee-X picks it (`xhw_video_boot`, `set_mode_480` and its `settings.c`
defaults): the settings only allow a mode, the dashboard has to allow it
too. With the defaults (`video_720p = 1`, `progressive = 1`, `widescreen =
2`: Auto) a boot runs 720p where the dashboard allows it on this AV pack
(component, `xbox_video_720p_allowed`) and 32 MB are free, else 480p where
the dashboard allows it (component, NTSC: `xbox_video_480p_allowed`), else
480i; 16:9 when the dashboard is set to widescreen, and always at 720p.
`xbox_video_output` is that rule; the boot logs the result and the
dashboard's flags as `[VIDEO]` after GPU init. An explicit choice wins:
Output 480i / 480p (`video_720p = 0`, `progressive` 0 / 1) stays at 480
whatever the dashboard says, Widescreen 4:3 / 16:9 pins the picture (16:9
at 480 even on a 4:3 dashboard, which Melee-X doesn't offer). There is no
explicit 720p: Auto gives it wherever it could run. The dashboard's
letterbox and 1080i flags are not used (Melee-X ignores them too).

Until `opt_version` 4 the defaults were 480 and 4:3, written into every
file. Version 4 moves `video_720p = 0` to 1 when `progressive = 1` (a file
with `progressive = 0` chose 480i, in the menu or by safe video) and
`widescreen = 0` to Auto, once; a 480p or 4:3 picked in an older build
can't be told from the old default and moves too. Melee-X made 720p its
default without moving older files. Kill switch: `-DXBOX_VIDEO_AUTO=0`
builds the old defaults and skips the move.

## Safe video

Holding BACK on any controller as the splash ends (the splash says so) runs
this boot at 480i and saves `video_720p = 0` and `progressive = 0` (Output
480i, which no default or migration undoes), so a TV that doesn't show the
dashboard's mode never stays black; Options > Video > Output goes back to
Auto (from Melee-X). The controllers are started before the
splash for it, opened during it and closed after; without a splash the boot
still waits 1.5 s for them. 480i on an HDTV pack set to 480p uses nxdk's
`XVideoInit` with the 640x480i mode (`xbox_video_set_480`), for the GPU and
for the splash and error screens alike. Other packs and PAL have no 480p, so
the Output row offers 480i/480p only on a component (HDTV) pack.

## Screen size

`pc_gx.c` draws into a logical screen of `g_pc_window_w` x `g_pc_window_h`:
640x480, or 854x480 for 16:9, where its hor+ correction widens the 3D view
and pillarboxes some 2D art (not all: the title logo comes out ~1.3x wide).
The GL shim scales viewports, scissors and read-backs onto the real
framebuffer: 854x480 onto 640x480 is the anamorphic squeeze a 16:9 TV
undoes; 720p is 1280x720 and always 16:9 (`renderer.md`). NES games are 4:3
between black bars inside it (`nes_aspect`, `pc_nes_fixnes.c`).

## Distribution

Users never compile. A release is a prebuilt `default.xbe` plus
`default.tbn` in one folder; the user adds their own disc image next to it.
For a disc, `tools/make-xiso` packs the XBE and the image into an XISO. By
default it trims the image to the ~28 MB the game reads
(`tools/gc_trim_ciso.py`), so the XISO fits a CD-R. We ship the script,
never an XISO.

## Decided against

- 64-bit build: the decomp assumes 32-bit pointers, and the Xbox is 32-bit.
- Using the RAM of a console upgraded to 128 MB (a Melee-X style `ram128`
  setting, bigger texture pool and ARAM cache): everything is built and
  tested for 64 MB, pbkit and the shim mask GPU addresses with `0x03FFFFFF`,
  emu64's `seg2k0` reads a pointer in `0x03000000-0x0FFFFFFF` as a segment
  address, and allocations that fail on 64 MB (and fall back) would succeed
  and move the heap. A 128 MB console runs as a 64 MB one (`memory.md`).
- Screenshots on BACK, as Melee-X does: BACK opens the pause menu here, so
  they're on the right stick click (tried both on 2026-10-03; the user kept
  the stick).
- The GameCube's passport-only trip (save the traveller to slot B, end the
  game): the PC port keeps the passport in memory only, so it led to the
  title screen. With no other town the Porter refuses instead.
- The PC port's GLSL shader path: nxdk has no GLSL compiler.
- pbgl for the renderer: replaced by the GL shim over pbkit.
- Cg for the vertex program: `cgc` does not run in the arm64 SDK image;
  `gx.vsh` is NV2A assembly.
- nxdk's `hal/audio`: its interrupt handler froze real hardware
  (`traps.md`).
- 720p at 32-bit colour: three 1280x720x32 framebuffers and the depth
  buffer need ~9.8 MB more than 480, over what's free on 64 MB. 720p runs
  at 16-bit colour (dithered) with Z16 depth and a smaller texture pool
  (`memory.md`).
- Automatic stick dead zone calibration: replayed on the hardware stick
  traces, a worn stick's rest point moves after every release (18-41%), so
  a learned value undershoots and walks the character on its own. The dead
  zone is a setting with a live stick readout instead.
- Turning on `PC_ENHANCEMENTS` for the whole tree: it also changes gameplay
  (camera, fog, player, submenus). Only the files listed in `patches.md` get it.
- `pc_vi.c`'s timer frame limiter on the Xbox: it lost every overrun and
  beat against the 59.94 Hz vblank; frames pace to the vblank counter
  (`perf.md`).
- A bigger pdclib buffer (`setvbuf`) for the disc image: `fseek` before
  every read discards it, so a 12-byte read would pull in 32 KB, and
  `fread` still copies byte by byte. Interposing `fseek` for every C file
  was rejected too (a program-wide stdio change for one reader): only
  `pc_disc.c` gets `xbox_disc_fread`/`xbox_disc_fseek`.

