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
