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
