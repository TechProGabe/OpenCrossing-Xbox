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
  stays silent without one; `boot.log` (until frame 600) and `hang.log` in
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
- **pbkit can hand you the buffer being scanned out** (two flips queued):
  wait for vblank until PCRTC_START moves off `pb_back_buffer()`.
- **Python `bytearray[a:b] = b""` deletes.** It broke the first XBE icon patch
  (every section offset shifted; kernel refused the XBE silently).
- **Worn Duke/S sticks rest 18–35% off centre and overshoot on release:** a
  12% per-axis deadzone reads that as walking. Measure with the L3 stick trace.
