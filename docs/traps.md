# Traps

Gotchas we paid for, some carried from the PC, Anbernic and Dreamcast
siblings. Read the section before touching that area; add new ones as you
find them.

## Build and toolchain

- **`TARGET_PC` isn't "PC".** It means "not GameCube" and guards the
  little-endian and 32-bit fixes. Removing it drops them.
- **UB flags.** `-fno-strict-aliasing -fwrapv` are load-bearing; plain `-O2`
  breaks the decomp (upstream commit `4f428276`).
- **TLUT endianness.** ROM TLUTs are big-endian, emu64/EFB TLUTs native
  little-endian: a per-slot `is_be` flag in `pc_gx_internal.h`.
- **jaudio map header.** The upstream decomp reads `AG.map_header` through
  big-endian casts; the PC side uses `Nas_MapHeaderReadByte`. Never take the
  decomp's side there in a merge.
- **Out-of-range float to integer is undefined behaviour, and clang uses
  it.** `(s16)32768.0f` folds to poison, and code whose result depends on
  it is deleted, silently and with no warning. The GameCube wraps it.
  `include/m_lib.h` converts through `int` (`patches.md`). To look for more:
  emit `-O0 -Xclang -disable-O0-optnone -S -emit-llvm` for every TU and grep
  for `poison`.
- **nxdk defines `_WIN32` and `_MSC_VER`.** `pc/` code under `#ifdef _WIN32`
  takes its Windows branch. `pc_card.c`'s save scan did, calling
  `FindFirstFileA` on a relative path that never went through
  `xbox_resolve`, so saves under any other name were never found; it's
  built `-U_WIN32` now (`patches.md`). The POSIX branches (`opendir`,
  `stat`, `mkdir`) are the ones `xbox_posix.c` resolves. Check which branch
  a `pc/` file takes before trusting it.
- **clang MS-compat include search.** nxdk targets `i386-pc-win32`; clang's
  MS mode searches the includer's includer dirs, so `"types.h"` resolved to
  `include/dolphin/types.h`. The build passes `-fno-ms-compatibility`.
- **...which breaks `windows.h` in C++.** `xboxkrnl.h` relies on MS-compat
  for `extern` arrays in C++, so `xbox/include/pc_platform.h` (a shadow of
  `pc/include/pc_platform.h`; keep them in sync) doesn't include
  `windows.h`.
- **Include `<xboxkrnl/xboxkrnl.h>` before `pc_platform.h`.** The decomp's
  `include/types.h` does `#define __declspec(x)`, so kernel data imports
  (`XboxKrnlVersion`, ...) become definitions: duplicate symbols at link.
- **`extern "C"` is file-scope only.** The decomp branches declare Xbox hooks
  at the top of the TU (or just above the function).
- **memcpy & co. are macros in C files** (`xbox_prelude.h`, `__builtin_*`).
  A declaration of one after the prelude breaks (`void* memcpy(...)`
  expands); the prelude includes `<string.h>` and the decomp's `_mem.h`
  first for that reason, and `xbox_mem.c`, which defines them, `#undef`s
  them. C++ is left alone: libc++ spells `std::memcpy`. `__builtin_memcmp`
  is still a call under `-ffreestanding`, even for 16 constant bytes;
  compare words in hot code (`words_eq` in `xbox_nv2a.c`).
- **C++ files don't get the prelude's path routing.** `fopen` & co. are only
  remapped for C (libc++ spells `std::fopen`), so a C++ file opening a
  relative path (`famicom.cpp`'s NES save) silently fails on the Xbox. Give
  such a file `fopen=xbox_fopen` (and friends) in CMake.
- **`set_source_files_properties(... COMPILE_OPTIONS ...)` replaces.** A
  per-file option added before the `PC_REUSED` line is wiped by it; append
  after it (`set_property(... APPEND ...)`), as the `-U_WIN32` for
  `pc_card.c` does.
- **CMake caches every `-D` option in the build dir.** A test build with
  `-DXBOX_WIDESCREEN=OFF` left the next "release" builds without it.
  `xbox/build.sh` passes each option's default on every run; add new options
  there too. `XBOX_BUILD_DIR` keeps test builds out of `build-xbox`.
- **`XBOX_CMAKE_ARGS` is word-split by the inner shell:** quote multi-flag
  values: `XBOX_CMAKE_ARGS="'-DCMAKE_C_FLAGS=-DA -DB'"`.
- **Debian clang 19.1.7 miscompiles** (nxdk warns, llvm#134607). The SDK
  image uses LLVM 21.
- **colima host.** `DOCKER_BUILDKIT=0`, no `--progress`, `bash -c` inside
  images, absolute paths in scripts.
- **Cg (`cgc`) is unusable in the arm64 SDK image** (i386 glibc and the amd64
  loader both fail). The vertex program is NV2A asm: temporaries r0-r11 only,
  one `c` and one `v` read per instruction, swizzled constants as
  `c[N].xxxy`.
- **nxdk zlib is `Z_SOLO`** without `compress.c`: use `deflate*` with your
  own allocator.
- **fixNES `DO_INLINE_ATTRIBS`** turns into `__forceinline` under `_MSC_VER`
  and emits no out-of-line copy; it's built without it.
- **pdclib's `errno` is a macro;** `padmgr.c` has an `errno` field.
  `xbox_decomp_prelude.h` undefines it for decomp TUs.
- **pdclib printf prints nothing for `%f`** (logs show `total=ms`).
- **`pc_gx_tev.c` is not built.** `xbox/src/xbox_gx_tev.c` replaces it (no
  GLSL).
- **Python `bytearray[a:b] = b""` deletes.** It broke the first XBE icon
  patch (every section offset shifted; the kernel refused the XBE
  silently).
- **Never commit ROMs, HDD images or BIOS files.** xemu needs an MCPX ROM, a
  BIOS and an HDD image: keep them outside the repo.

## Memory and pointers

- **seg2k0 tells pointers from N64 segment addresses by range.** Anything
  from `0x03000000` to `0x0FFFFFFF` outside the XBE image is read as a
  segment address, so the game's memory must stay below 64 MB (a 128 MB
  console runs as 64 MB: `memory.md`).
- **`u8` texture arrays can sit at odd addresses.** clang's MS-ABI target
  gives a `u8[]` alignment 1 (GCC on Linux pads big arrays to 32), so a
  texture like `obj_s_douzou_b3_tex_pic_i4` landed at `...469`. Runtime GBI
  macros turn odd pointers into tokens (`pc_gbi_runtime.c`, `0x02F00000 +
  2n`) that `seg2k0` didn't unpack for segment bases (whole-console crash
  when the station statues were drawn); static display lists carry no tag,
  so `seg2k0` dropped the low bit (texture shifted a byte). Fixed twice:
  `seg2k0` unpacks tokens, and `src/data` builds with `-fcommon` so the
  arrays are aligned. Keep both. You can't tell a static display list by
  its address: game-built ones in BSS buffers are in the image too.
- **Freed memory is gone on the Xbox.** A PC keeps freed and unused heap
  pages readable; the Xbox kernel decommits them, so a stale or bogus
  pointer that "worked" on a PC is a page fault here, and every thread runs
  in kernel mode: unhandled, that's a bugcheck (frozen or rebooting
  console, no logs). `xbox_crash.c` catches it first; `xbox_ptr_readable`
  guards texture images and display lists.
- **128 MB in xemu needs a 128 MB-aware BIOS.** At `[sys] mem_limit =
  '128'` the Complex 4627 BIOS still reports 64 MB; Cerbios reports
  131072 KB but, honouring the XBE's "limit to 64 MB" flag (cxbe always
  sets it), keeps the upper 64 MB from the game. To test a kernel that
  hands it out, clear bit `0x4` of the dword at `0x124` in a copy of the
  XBE (`memory.md`).

## GPU (NV2A and pbkit)

- **pbkit turns the w-buffer on whenever it targets a buffer.**
  `pb_target_back_buffer` sets CONTROL0 to `0x00110001`
  (`Z_PERSPECTIVE_ENABLE`), so state sent once at init is gone from the
  next frame. With our screen-z vertex program that stored the eye distance
  as an integer: z-fighting on the villager and the menu glove at 480 and
  720p. `frame_open` resends CONTROL0 (`renderer.md` "Depth"). xemu
  emulates the w-buffer, so it shows this too.
- **pbkit halts the machine on a GPU error.** Its DPC switched to the debug
  screen and looped on `Sleep()` at DISPATCH_LEVEL, and several
  interrupt-time waits spin unbounded. pbkit is built from source through
  `tools/xbox/patch_pbkit.py` (record and acknowledge, bounded waits).
- **pbkit's pushbuffer has no overflow check** (release build): writing past
  `pb_size` walks into the next contiguous allocation. `xbox_nv2a.c`
  restarts at the head when a frame nears the end (`XBOX_PB_GUARD`).
- **`pb_busy` is not idle.** It compares GET with PUT and reads PGRAPH's
  status only: methods already in PFIFO's CACHE1 pass. Anything that frees
  or rewrites GPU-read memory after a wait needs `wait_idle`'s strict check.
- **pbkit's vblank event is pulsed** (`NtPulseEvent`): it wakes only threads
  already waiting. Read the counter, then `pb_wait_for_vbl()`, and a vblank
  in between costs a whole frame. `vbl_pace` waits with a timeout
  (`ocx_pb_wait_for_vbl_timeout`, added by `patch_pbkit.py`) and re-reads.
- **pbkit can hand you the buffer being scanned out** (two flips queued):
  wait for vblank until PCRTC_START moves off `pb_back_buffer()`.
- **`XVideoWaitForVBlank` before `pb_init`** hooks the GPU IRQ: `pb_init`
  returns -4.
- **`XVideoSetMode` frees the previous XVideo framebuffer.** Anything holding
  the old pointer (the splash) must drop it first (`xbox_splash_release`).
- **NV2x colour and depth widths should match.** 16-bit colour (720p) is
  paired with Z16, not pbkit's fixed Z24S8. xemu accepts the mismatch, so it
  proves nothing here. pbkit's `pb_erase_depth_stencil_buffer` writes
  `0xffffff00` (Z24S8 layout); for Z16 the shim clears with its own value.
- **Texture FORMAT bit 3 (BORDER_SOURCE) must be 1 (colour).** 0 means
  "image carries border texels": text and sprites render as shifted,
  repeated chunks with checkerboard garbage.
- **`TEXTURE_CONTROL1` pitch must be a multiple of 64**, even for swizzled
  textures that don't use it. A 4x4 AY8 gave 4 and a PGRAPH data error on
  hardware.
- **Specular (V1) into the combiners needs `SPECULAR_ENABLE=1` and
  `LIGHT_CONTROL ALPHA_FROM_MATERIAL_SPECULAR`,** even with a vertex
  program; otherwise oD1 becomes (0,0,0,1). Fog rides in V1.a, so the whole
  scene came out solid fog colour (xemu `pgraph/glsl/vsh.c`).
- **FRONT_FACE is CCW** even though the viewport y-flip is folded into the
  projection. CW culled every front face: black ground, missing logo.

## Audio

- **nxdk hal/audio freezes a real Xbox:** its level-triggered IRQ 6 handler
  and interrupt-enabled DMA locked the machine at XAudioPlay (it never fires
  in xemu). `xbox_audio.c` drives the ACI polled, no interrupt connected.
- **The AC97 IRQ never fires in xemu,** so nxdk SDL audio callbacks stall
  and the game waits forever in `Na_CheckRestartReady`. `xbox_audio.c`
  polls CIV.
- **A codec left stuck by an unclean end stays stuck** until a full
  power-off (`[AUDIO] AC97 stuck: civ 0`); reset and IGR don't clear it.
- **Never use `SDL_Atomic*` on nxdk.** They're one global spinlock whose
  contention path is `SDL_Delay(0)` (yields only to equal or higher
  priority). Game thread preempted inside it plus the high-priority AC97
  pump spinning = the whole game livelocked, randomly 5 s to minutes in.
  Use `__atomic_load_n` / `__atomic_store_n`.
- **Threads that feed the hardware need more than the game's priority.** A
  frame that misses the vblank never sleeps, so an equal-priority thread
  waits out the game's whole time slice: the audio producer let the AC97
  ring run dry at 51 fps (v4). It runs one step above now, the AC97 pump
  above that; both sleep when they're ahead, and their locks must block in
  the kernel (SDL mutexes do), never spin.
- **xemu's AC97 codec boots muted.** xemu reuses QEMU's `hw/audio/ac97.c`,
  whose mixer reset is Master `0x8000` / PCM-out `0x8808` (mute bits). The
  retail WM9709 has no mixer registers and nxdk never writes them.
  `AIInit` writes both to 0.
- **macOS xemu never plays AC97.** No CoreAudio is linked and QEMU's SDL
  driver is disabled, so the ac97 voice goes to `none`. Only the MCPX APU
  is audible, so `xbox_audio.c` uses an APU voice when the codec ID says
  xemu.
- **APU PCM voices: `SAMPLES_PER_BLOCK` = channels - 1.** xemu's block size
  is container × samples_per_block (no channel term); stereo with 0 steps 2
  bytes per frame: half-speed, garbled audio. Default `use_dsp=false` =
  MON_VP: voices are heard without any GP DSP program (real hardware needs
  one). Check guest audio with `-monitor unix:<sock>,server,nowait`, then
  `wavcapture <path> #default`. `-DXBOX_DBG_AUDIO` logs CIV/LVI/peak every
  2 s.

## Input

- **Worn Duke/S sticks rest 18-41% off centre and overshoot on release:** a
  12% per-axis dead zone reads that as walking, and AC turns the C-stick
  into the C buttons past ~23% (choice lists scroll on C-down). Both sticks
  default to 40%. Measure with the L3 stick trace (`stickN.log`).
- **PADRead runs about twice per frame.** Autopad script call numbers are
  PADRead calls, not frames; menu cooldowns (8-15 frames) need ~40 calls
  between presses.

## Files, saves and settings

- **No cwd on the Xbox.** Relative paths go through `xbox_resolve()`: disc
  reads to `D:\`, writes to `E:\UDATA\4f430001\`.
- **D: is only mounted if `libnxdk_automount_d.lib` is linked with
  `-include:_automount_d_drive`** (nxdk's Makefile does it; our CMake must
  too). Without it `FindFirstFile("D:\\*")` fails with 2 while nothing else
  looks wrong. nxdk wants the `*.*` form; a bare `*` fails too.
- **cxbe has no TitleID flag.** The UDATA folder name is fixed in
  `xbox_io.h`.
- **pdclib `fread` is 1 KB at a time.** Its stdio buffer is `BUFSIZ` 1024,
  `fread` refills it with one `NtReadFile` per KB and copies byte by byte,
  and `fseek` discards it: the disc image read at ~2.5 MB/s on hardware.
  `pc_disc.c` is built with `XBOX_DISC_TU`, so its `fread`/`fseek` are
  `xbox_disc_fread`/`xbox_disc_fseek` (`ReadFile` and `SetFilePointer`);
  don't mix other stdio calls into that stream.
- **nxdk winapi has no `FlushFileBuffers`:** use `NtFlushBuffersFile` (its
  HANDLEs are NT handles), `xbox_flush_file()`.
- **The PC settings writer rewrites all of `settings.ini`.** Keys it doesn't
  know are dropped; `xbox_settings.c` appends `[Xbox]` after every save.
- **A new default for a key that's already written stays unseen.** The
  `[Xbox]` writer puts every key in the file, so consoles keep the old
  default (`gpu_overlap = 0`, `video_720p = 0`). Migrate with `opt_version`.
- **nxdk's `mktime` returns -1.** The PC port took its time-zone offset from
  `difftime(now, mktime(gmtime(now)))`, which became the whole Unix time:
  the game clock read ~2083 and moved on by the time between boots. The
  Xbox takes the offset from `GetTimeZoneInformation` (the dashboard's
  zone, `xbox_local_offset_secs`). Any other `mktime` use: check for -1.

## Kernel, timing and launching

- **Timer frequencies.** `KeQueryPerformanceCounter` (`xbox_ticks`) is the
  ACPI timer, 3.375 MHz from a crystal, on any CPU. nxdk's
  `QueryPerformanceCounter` (SDL's) is the CPU's TSC, and its frequency is
  worked out from the multiplier and FSB with a 733 MHz fallback; on a CPU
  upgrade that guess can be wrong (`xbox_clock_check`). Don't time anything
  against `KeQueryInterruptTime` in xemu: it runs ~7% apart from both
  counters there.
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
- **`XLaunchXBE` wants a `\Device\` path.** `D:\default.xbe` turns into
  `\??\D:;default.xbe` in the launch data, the launch fails, and the
  fallback is the dashboard: the Options menu's restart went to the
  dashboard until it used `XeImageFileName` as it is (from Melee-X's
  `xhw_reboot_self`).
- **xemu detection:** CPUID leaf 1 EDX bit 1 (VME) is clear in xemu
  (0383f9fd) and set on the Xbox (0383f9ff); both report signature 0x68a.
  Never probe the AC97 codec to find out. xemu also answers microcode
  revision 1 (MSR 8Bh) and kernel 1.0.4627.1 (`xbox_diag.c`).
- **Catching a fault and carrying on.** An SEH handler on fs:[0]
  (`xbox_crash.c` style) may `longjmp` straight out of the kernel's
  dispatcher: that is what an `__except` block does after `RtlUnwind`, and
  with nothing between the two frames there is nothing to unwind. Put
  fs:[0] back by hand afterwards: the dispatcher pushed a registration of
  its own before calling the handler, and it is still there. Verified in
  xemu and on red (kernel 5101, 2026-10-04; `xbox_diag.c` `diag_try`, the
  `[DIAG] guard self-test` line); the handler must return
  `ExceptionContinueSearch` (1) for `EXCEPTION_UNWIND` calls. Ring 0 lets
  game code `rdmsr`/`wrmsr`/`wbinvd`/read `cr0`; an unknown MSR is a #GP
  on hardware, which the kernel hands over as `c0000005`, a read of 0
  (MSR 1A0h on the stock cD0), and silence in xemu. Guard it.
- **The kernel patches the XBE in RAM.** The import thunk table (here in
  `.rdata`, `PointerToKernelThunkTable` XOR `0x5B6D40B6` for a retail
  header) is overwritten with kernel addresses at load, so a RAM-vs-disk
  compare of the sections has to skip it. `.text` is otherwise identical to
  the file (xemu, Cerbios-free BIOS): any other difference is a patcher.
- **Something patches rdtsc on CPU-upgraded consoles.** BIOS or modchip
  firmware rewrites `0f 31` (rdtsc) as `cd 2e` (int 2Eh) in a title's
  `.text` by signature, and the signature can match bytes that straddle two
  instructions: GitHub #3 crashed on `jne +0f; xor esi, esi`
  (`known-issues.md`). `xbox_code_repair.c` puts `.text` back from the
  file at boot. Don't trust `.text` before that point on such consoles.
- **`.text` is read-only, in ring 0 too.** The kernel maps it read-only and
  CR0.WP is set, so a store into code faults; `MmSetAddressProtect` on the
  page did not make it writable (xemu, 2026-10-05). Clear CR0.WP around the
  store with interrupts off (`xbox_code_repair.c` `put_byte`).

## Logs, screenshots and testing

- **Real hardware has no COM1** (retail board; what the absent port reads
  is up to the board and modchip). `xbox_io.c` probes the UART scratch
  register and stays silent without one; on the console the logs are files
  in `E:\UDATA\4f430001\` and the crash and hang reporters draw on screen.
  nxdk's stdout and stderr are dead handles.
- **Which hardware log covers what:** `boot.log` holds every line of the
  session's first 4 MB, then `boot2.log` / `boot3.log` alternate (2 MB
  each), so a late hang is in whichever was written last; after frame 120
  they're up to a second behind (queued, written by the watchdog).
  `last.log` is the last 4 KB plus `[STATE]`, rewritten within 3 s of any
  non-heartbeat line, and `perf.log` the whole session a minute at a time.
  Each boot renames the previous boot's logs to `*_prev.log` (one
  generation; a log the boot didn't write, such as `crash.log`, keeps its
  `_prev` copy), so a relaunch loses the logs of two boots back: pull
  before relaunching twice.
- **The `cpu` in `[HITCH]` / `perf.log` is time between presents,** not CPU
  used by the game thread: another thread's work (the audio producer
  catching up, a disc read) and any wait count in it.
- **xemu has no screenshot command** (no QMP or monitor `screendump`, and no
  host screen capture permission). Screenshots there go over COM1
  (`XBOX_FBDUMP_EVERY`, autopad `SHOT`, `tools/xbox/fbdump_to_png.py`) and
  freeze the game for seconds per frame; on the console the screenshots
  setting writes BMPs to UDATA in a quarter of a second.
- **Serial interleaving:** other threads' logs can splice into a dump line;
  `xbox_log_exclusive()` holds COM1 during `xbox_fbdump()`.
- **xemu EEPROM video flags are at 0x94** (0x90 is the language), in the
  encoder-settings bit layout (0x00020000 = 720p, 0x00080000 = 480p,
  0x00010000 = widescreen); the user-section checksum at 0x60 covers
  0x64-0xBF. xemu runs a copy via `-config_path` (`toolchain.md`).
- **The "pristine" test HDD isn't empty.** `~/xemu/oc-720/hdd.pristine.qcow2`
  holds the Area 51 town and a `settings.ini` (720p, widescreen, FPS
  counter) in `E:\UDATA\4f430001`. For a first boot build with
  `-DXBOX_DBG_FRESH_UDATA`.
- **Another session's xemu harness may `pkill -9` xemu.** Two sessions on
  one Mac killed each other's runs (2026-10-03); check `pgrep -fl
  MacOS/xemu` before a run and never kill a run you didn't start.
