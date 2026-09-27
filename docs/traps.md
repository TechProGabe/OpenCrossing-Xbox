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
