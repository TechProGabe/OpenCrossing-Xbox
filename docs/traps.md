# Traps

Known gotchas, most carried from the PC/Anbernic/DC siblings. Add new ones as paid for.

- **seg2k0 pointer heuristic.** emu64 distinguishes real pointers from N64
  segment addresses by range; PC mmaps the arena at ≥ `0x10000000`. The Xbox
  arena must also sit at a fixed high VA (`NtAllocateVirtualMemory` with a base
  hint). A low heap address → display lists silently resolve wrong.
- **`TARGET_PC` ≠ PC.** It means "not GameCube". Removing it drops LE fixes.
- **UB flags.** `-fno-strict-aliasing -fwrapv` are load-bearing; plain `-O2`
  breaks the decomp (upstream commit `4f428276`).
- **TLUT endianness.** ROM TLUTs are BE, emu64/EFB TLUTs native LE — per-slot
  `is_be` flag in `pc_gx_internal.h`.
- **jaudio map header.** Upstream decomp reads `AG.map_header` via BE casts;
  the PC side uses `Nas_MapHeaderReadByte`. Never take the decomp side there.
- **Never commit ROMs/HDD images/BIOS.** xemu needs MCPX ROM + BIOS + HDD
  image — keep them outside the repo.
- **colima host.** `DOCKER_BUILDKIT=0`, no `--progress`, `bash -c` inside
  images, absolute paths in scripts.
- **clang MS-compat include search.** nxdk targets `i386-pc-win32`; clang's
  MS mode searches the includer's includer dirs, so `"types.h"` resolved to
  `include/dolphin/types.h`. The build passes `-fno-ms-compatibility`.
- **…which breaks `windows.h` in C++.** `xboxkrnl.h` relies on MS-compat for
  `extern` arrays in C++. So `xbox/include/pc_platform.h` (a shadow of
  `pc/include/pc_platform.h` — keep in sync) does not include `windows.h`.
- **Debian clang 19.1.7 miscompiles** (nxdk warns, llvm#134607). SDK image uses LLVM 21.
- **nxdk stdout/stderr are dead handles.** Logging = COM1 via `xbox_io.c`;
  xemu needs `-device lpc47m157 -serial file:...` (the harness adds it).
- **No QMP screendump in xemu, no host screen capture permission.** Screenshots
  = `xbox_fbdump()` over COM1 → `tools/xbox/fbdump_to_png.py`.
- **nxdk zlib is `Z_SOLO`** without `compress.c`: use `deflate*` + own allocator.
- **fixNES `DO_INLINE_ATTRIBS`** → `__forceinline` under `_MSC_VER` emits no
  out-of-line copy; built without it.
- **pdclib `errno` is a macro;** `padmgr.c` has an `errno` field.
  `xbox_decomp_prelude.h` undefines it for decomp TUs.
- **No cwd on Xbox.** Relative paths go through `xbox_resolve()`: disc reads →
  `D:\`, writes → `E:\UDATA\4f430001\`.
- **cxbe has no TitleID flag.** UDATA dir name is fixed in `xbox_io.h`.
- **D: is only mounted if `libnxdk_automount_d.lib` is linked with
  `-include:_automount_d_drive`** (nxdk's Makefile does it; our CMake must too).
  Without it `FindFirstFile("D:\\*")` fails with 2 while nothing else looks wrong.
- **pdclib `fread` is 1 KB at a time.** Its stdio buffer is `BUFSIZ` 1024
  and `fread` refills it with one `NtReadFile` per KB and copies byte by
  byte, and `fseek` discards the buffer: the disc image read at ~2.5 MB/s
  on hardware. `pc_disc.c` is built with `XBOX_DISC_TU`, so its
  `fread`/`fseek` are `xbox_disc_fread`/`xbox_disc_fseek` (`ReadFile` and
  `SetFilePointer` on the handle, `XBOX_DISC_DIRECT`); don't mix other
  stdio calls into that stream. A bigger `setvbuf` would still copy byte by
  byte and read 32 KB for a 12-byte `fseek`+`fread`.
- **pdclib printf prints nothing for `%f`** (logs show `total=ms`). Log-only for now.
- **`pc_gx_tev.c` is not built** — `xbox/src/xbox_gx_tev.c` replaces it (no GLSL).
- **NV2A texture FORMAT bit 3 (BORDER_SOURCE) must be 1 (colour).** 0 means
  "image carries border texels" → text/sprites render as shifted, repeated
  chunks with checkerboard garbage.
- **Specular (V1) into combiners needs `SPECULAR_ENABLE=1` and
  `LIGHT_CONTROL ALPHA_FROM_MATERIAL_SPECULAR`,** even with a vertex program;
  otherwise oD1 becomes (0,0,0,1). We carry fog in V1.a → whole scene came out
  solid fog colour (xemu `pgraph/glsl/vsh.c`).
- **FRONT_FACE is CCW** even though the viewport y-flip is folded into the
  projection. CW culled every front face: black ground, missing logo.
- **`XVideoWaitForVBlank` before `pb_init`** hooks the GPU IRQ → `pb_init` −4.
- **AC97 IRQ never fires in xemu** → nxdk SDL audio callbacks stall and the
  game waits forever in `Na_CheckRestartReady`. `xbox_audio.c` polls CIV.
- **Cg (`cgc`) is unusable in the arm64 SDK image** (i386 glibc / amd64 loader
  both fail). Vertex program is NV2A asm: temporaries r0–r11 only, one `c` and
  one `v` read per instruction, swizzled constants as `c[N].xxxy`.
- **Serial interleaving:** other threads' logs can splice into a dump line;
  `xbox_log_exclusive()` holds COM1 during `xbox_fbdump()`.
- **`XBOX_CMAKE_ARGS` is word-split by the inner shell:** quote multi-flag
  values: `XBOX_CMAKE_ARGS="'-DCMAKE_C_FLAGS=-DA -DB'"`.
- **`extern "C"` is file-scope only** — the decomp branches declare Xbox
  hooks at the top of the TU.
- **xemu's AC97 codec boots muted.** xemu reuses QEMU `hw/audio/ac97.c`, whose
  mixer reset is Master `0x8000` / PCM-out `0x8808` (mute bits). The retail
  WM9709 has no mixer registers and nxdk never writes them, so DMA runs at
  48 kHz with real samples and nothing is heard. `AIInit` writes both to 0.
- **Never use `SDL_Atomic*` on nxdk.** They are one global spinlock whose
  contention path is `SDL_Delay(0)` (yields only to ≥ priority). Game thread
  preempted inside it + high-priority AC97 pump spinning = whole game livelocked,
  randomly 5 s–minutes in. Use `__atomic_load_n/__atomic_store_n`.
- **macOS xemu never plays AC97.** No CoreAudio linked, QEMU SDL driver
  disabled → the ac97 voice goes to `none`. Only the MCPX APU is audible, so
  `xbox_audio.c` uses an APU voice when the codec ID says xemu.
- **APU PCM voices: `SAMPLES_PER_BLOCK` = channels − 1.** xemu's block size is
  container × samples_per_block (no channel term); stereo with 0 steps 2 bytes
  per frame → half-speed, garbled audio. Default `use_dsp=false` = MON_VP:
  voices are heard without any GP DSP program (real hardware needs one).
  Verify guest audio instead: `-monitor unix:<sock>,server,nowait`, then
  `wavcapture <path> #default`. `-DXBOX_DBG_AUDIO` logs CIV/LVI/peak every 2 s.
- **Include `<xboxkrnl/xboxkrnl.h>` before `pc_platform.h`.** The decomp's
  `include/types.h` does `#define __declspec(x)`, so kernel data imports
  (`XboxKrnlVersion`, …) become definitions → duplicate symbols at link.
- **Real hardware has no COM1** (retail board; what the absent port reads is
  up to board + modchip). `xbox_io.c` probes the UART scratch register and
  stays silent without one; `boot.log` (until frame 120) and `hang.log` in
  `E:\UDATA\4f430001\` plus the watchdog's on-screen report replace it.
- **nxdk winapi has no `FlushFileBuffers`:** use `NtFlushBuffersFile` (its
  HANDLEs are NT handles) — `xbox_flush_file()`.
- **xemu has no QEMU `screendump`** monitor command.
- **nxdk hal/audio freezes a real Xbox:** its level-triggered IRQ 6 handler +
  interrupt-enabled DMA locked the machine at XAudioPlay (never fires in
  xemu). `xbox_audio.c` drives the ACI polled, no interrupt connected.
- **xemu detection:** CPUID leaf 1 EDX bit 1 (VME) — clear in xemu (0383f9fd),
  set on the Xbox (0383f9ff); both report signature 0x68a. Never probe the
  AC97 codec to find out.
- **pbkit's vblank event is pulsed** (`NtPulseEvent`): it wakes only threads
  already waiting. Read the counter, then `pb_wait_for_vbl()`, and a vblank
  in between costs a whole frame. `vbl_pace` waits with a timeout
  (`ocx_pb_wait_for_vbl_timeout`, added by `patch_pbkit.py`) and re-reads.
- **pbkit can hand you the buffer being scanned out** (two flips queued):
  wait for vblank until PCRTC_START moves off `pb_back_buffer()`.
- **Python `bytearray[a:b] = b""` deletes.** It broke the first XBE icon patch
  (every section offset shifted; kernel refused the XBE silently).
- **`u8` texture arrays can sit at odd addresses.** clang's MS-ABI target
  gives a `u8[]` alignment 1 (GCC on Linux pads big arrays to 32), so a
  texture like `obj_s_douzou_b3_tex_pic_i4` landed at `…469`. Runtime GBI
  macros turn odd pointers into tokens (`pc_gbi_runtime.c`, `0x02F00000 +
  2n`) that `seg2k0` didn't unpack for segment bases (whole-console crash
  when the station statues were drawn); static display lists carry no tag,
  so `seg2k0` dropped the low bit (texture shifted a byte). Fixed twice:
  `seg2k0` unpacks tokens, and `src/data` builds with `-fcommon` so the
  arrays are aligned. Keep both. You can't tell a static display list by
  its address: game-built ones in BSS buffers are in the image too.
- **Freed memory is gone on the Xbox.** A PC keeps freed/unused heap pages
  readable; the Xbox kernel decommits them, so a stale or bogus pointer that
  "worked" on PC is a page fault here, and every thread runs in kernel mode:
  unhandled, that is a bugcheck (frozen or rebooting console, no logs).
  `xbox_crash.c` catches it first.
- **pbkit halts the machine on a GPU error.** Its DPC switched to the debug
  screen and looped on `Sleep()` at DISPATCH_LEVEL, and several interrupt-time
  waits spin unbounded. We build pbkit from source through
  `tools/xbox/patch_pbkit.py` (record + acknowledge, bounded waits).
- **pbkit's pushbuffer has no overflow check** (release build): writing past
  `pb_size` walks into the next contiguous allocation. `xbox_nv2a.c` restarts
  at the head when a frame nears the end (`XBOX_PB_GUARD`).
- **Worn Duke/S sticks rest 18–35% off centre and overshoot on release:** a
  12% per-axis deadzone reads that as walking. Measure with the L3 stick trace.
- **Quit never returns to `main_body`.** `src/main.c` (TARGET_PC) calls
  `pc_platform_shutdown()` then `exit(0)` after the game loop; nxdk's exit
  reboots (a disc relaunches itself). The Xbox `pc_platform_shutdown` goes
  to the dashboard instead.
- **`XLaunchXBE` doesn't reset the devices.** It quick-reboots into the next
  XBE while anything still doing DMA keeps going. pbkit stops the GPU from a
  shutdown notification; nothing stops the AC97 (it loops its last buffer)
  or the USB host controller (OHCI writes its frame counter and done queue
  to RAM every millisecond; SDL's joystick quit leaves it running on
  purpose). On hardware the second quit in one power-on hung on a looping
  sound, then error 21. `leave_game` (`xbox_settings.c`) stops both first.
- **Out-of-range float to integer is undefined behaviour, and clang uses
  it.** `(s16)32768.0f` folds to poison, and code whose result depends on
  it is deleted, silently and with no warning (`-w` hides nothing here;
  there is none). The GameCube wraps it. `include/m_lib.h` converts through
  `int` (`patches.md`). To check for more: emit `-O0 -Xclang
  -disable-O0-optnone -S -emit-llvm` for every TU and grep for `poison`.
- **The PC settings writer rewrites all of `settings.ini`.** Keys it doesn't
  know are dropped; `xbox_settings.c` appends `[Xbox]` after every save.
- **PADRead runs about twice per frame.** Autopad script call numbers are
  PADRead calls, not frames; menu cooldowns (8-15 frames) need ~40 calls
  between presses.
- **xemu EEPROM video flags are at 0x94** (0x90 is the language), in the
  encoder-settings bit layout (0x00020000 = 720p, 0x00080000 = 480p,
  0x00010000 = widescreen); the user-section checksum at 0x60 covers
  0x64-0xBF. xemu runs a copy via `-config_path` (`toolchain.md`).
- **NV2x colour and depth widths should match.** 16-bit colour (720p) is
  paired with Z16, not pbkit's fixed Z24S8. xemu accepts the mismatch, so it
  proves nothing here. pbkit's `pb_erase_depth_stencil_buffer` writes
  `0xffffff00` (Z24S8 layout); for Z16 the shim clears with its own value.
- **XVideoSetMode frees the previous XVideo framebuffer.** Anything holding
  the old pointer (the splash) must drop it first (`xbox_splash_release`).
- **CMake caches every `-D` option in the build dir.** A test build with
  `-DXBOX_WIDESCREEN=OFF` left the next "release" builds without it.
  `xbox/build.sh` passes each option's default on every run; add new
  options there too.
- **nxdk defines `_WIN32` and `_MSC_VER`.** `pc/` code under `#ifdef _WIN32`
  takes its Windows branch: `pc_card.c`'s save scan calls `FindFirstFileA`
  with a relative path that never goes through `xbox_resolve`, so it finds
  nothing (`known-issues.md`). The POSIX branches (`opendir`, `stat`, `mkdir`)
  are the ones `xbox_posix.c` resolves. Check which branch a `pc/` file
  takes before trusting it.
- **The `cpu` in `[HITCH]` / `perf.log` is time between presents,** not CPU
  used by the game thread: another thread's work (the audio producer
  catching up, a disc read) and any wait count in it. The cold-boot title
  chug read as "cpu 80 ms" with the game thread mostly starved. Before
  vblank pacing it also included `pc_vi.c`'s pacing spin.
- **Which hardware log covers what:** `boot.log` holds every line of the
  session's first 4 MB, then `boot2.log` / `boot3.log` alternate (2 MB each),
  so a late hang is in whichever was written last; after frame 120 they are
  up to a second behind (queued, written by the watchdog). `last.log` is the
  last 4 KB plus `[STATE]`, rewritten within 3 s of any non-heartbeat line,
  and `perf.log` the whole session a minute at a time. Each boot renames
  the previous boot's logs to `*_prev.log` (one generation; a log the boot
  didn't write, such as `crash.log`, keeps its `_prev` copy), so a relaunch
  loses the logs of two boots back: pull before relaunching twice.
- **memcpy & co. are macros in C files** (`xbox_prelude.h`, `__builtin_*`).
  A declaration of one after the prelude breaks (`void* memcpy(...)` expands);
  the prelude includes `<string.h>` and the decomp's `_mem.h` first for that
  reason, and `xbox_mem.c`, which defines them, `#undef`s them. C++ is left
  alone: libc++ spells `std::memcpy`. `__builtin_memcmp` is still a call
  under `-ffreestanding`, even for 16 constant bytes: clang only expands it
  when memcmp counts as a library builtin. Compare words in hot code
  (`words_eq` in `xbox_nv2a.c`).
- **Threads that feed the hardware need more than the game's priority.**
  A frame that misses the vblank never sleeps, so an equal-priority thread
  waits out the game's whole time slice: the audio producer let the AC97
  ring run dry at 51 fps (v4). It runs one step above now, the AC97 pump
  above that; both sleep when they're ahead, and their locks must block in
  the kernel (SDL mutexes do), never spin (`SDL_Atomic*`, above).
- **`pb_busy` is not idle.** It compares GET with PUT and reads PGRAPH's
  status only: methods already in PFIFO's CACHE1 pass. Anything that frees
  or rewrites GPU-read memory after a wait needs `wait_idle`'s strict check.
- **A new default for a key that is already written stays unseen.** The
  `[Xbox]` writer puts every key in the file, so consoles keep the old
  default (`gpu_overlap = 0`). Migrate with a version key (`opt_version`).
- **Another session's xemu harness may `pkill -9` xemu.** Two sessions on one
  Mac killed each other's runs (2026-10-03); check `pgrep -fl MacOS/xemu`
  before a run and never kill a run you didn't start.
- **C++ files don't get the prelude's path routing.** `fopen` & co. are only
  remapped for C (libc++ spells `std::fopen`), so a C++ file opening a
  relative path (`famicom.cpp`'s NES save) silently fails on the Xbox. Give
  such a file `fopen=xbox_fopen` (and friends) in CMake.
- **128 MB in xemu needs a 128 MB-aware BIOS.** At `[sys] mem_limit =
  '128'` the Complex 4627 BIOS still reports 64 MB; Cerbios reports
  131072 KB but, honouring the XBE's "limit to 64 MB" flag (cxbe always
  sets it), keeps the upper 64 MB from the game. To test a kernel that
  hands it out, clear bit `0x4` of the dword at `0x124` in a copy of the
  XBE. Contiguous allocations between 64 and 128 MB (Melee-X's hold) got
  4 KB there; `xbox_ramlock.c` commits pages and keeps the high ones
  (`memory.md`).
- **Timer frequencies.** `KeQueryPerformanceCounter` (`xbox_ticks`) is the
  ACPI timer, 3.375 MHz from a crystal, on any CPU. nxdk's
  `QueryPerformanceCounter` (SDL's) is the CPU's TSC, and its frequency is
  worked out from the multiplier and FSB with a 733 MHz fallback; on a CPU
  upgrade that guess can be wrong (`xbox_clock_check`). Don't time anything
  against `KeQueryInterruptTime` in xemu: it runs ~7% apart from both
  counters there.
- **nxdk's `mktime` returns -1.** The PC port took its time-zone offset
  from `difftime(now, mktime(gmtime(now)))`, which became the whole Unix
  time: the game clock read ~2083 and moved on by the time between boots.
  The Xbox takes the offset from `GetTimeZoneInformation` (the dashboard's
  zone, `xbox_local_offset_secs`). Any other `mktime` use: check for -1.
