# Platform API — overview, file reuse, landmines, gaps

Counts and dispositions for the 698 symbols `pc/src/` defines, which `pc/` files
survive on Dreamcast, the ranked semantic landmines, and what this recon pass did
not verify. **Read first** before any other `platform-api-*` file — it carries the
symbol-table legend the per-subsystem tables use.
Split out of `kb/design-platform-api.md` (§0, §2, §4, §5 legend, §7).

## 0. Summary

| | count |
|---|---|
| Non-static symbols defined by `pc/src/` | **698** |
| …referenced by name somewhere in `src/` or `include/` | 589 |
| …`pc/`-internal only (not named by game code) | 109 (marked ✱ in the tables) |
| Distinct subsystem groups | 55 |

Dispositions:

| disposition | count | meaning |
|---|---|---|
| `rewrite-for-KOS` | **421** | needs a genuinely new DC implementation |
| `port-as-is` | **146** | the body compiles and behaves correctly on sh-elf unchanged |
| `drop` | **97** | no caller in the DC build; delete rather than port |
| `stub-and-log` | **34** | keep an empty body, add a log so we notice if it is ever hit |

The 421 "rewrite" figure is dominated by the GX surface (≈300 symbols) —
most of those are one-line state setters whose rewrite is mechanical. The
genuinely hard rewrites are enumerated in §4.

### What the build actually compiles (from `pc/CMakeLists.txt`)

`GLOB_RECURSE src/**.c` + `src/**.cpp`, then these are **excluded** and must
therefore be reimplemented by the platform layer:

- Whole Dolphin SDK subtrees: `dolphin/{gx,os,vi,pad,card,dvd,ai,ar,dsp,exi,si,db,mtx,base,gba,amcstubs,OdemuExi2,odenotstub}`
  — everything in the tables below with a `GX*`/`OS*`/`VI*`/`PAD*`/`CARD*`/`DVD*`/`AI*`/`DSP*`/`AR*`/`EXI*`/`SI*`/`DB*`/`PSMTX*`/`C_MTX*` name.
- **Exception:** `src/static/dolphin/pad/Padclamp.c` is explicitly added back
  into `PC_SOURCES`. It is the ONE Dolphin SDK source file the port keeps
  (`JUTGamePad.cpp` calls `PADClamp`). `dc/` must do the same.
- PPC/CodeWarrior runtime: `TRK_MINNOW_DOLPHIN/`, `Runtime.PPCEABI.H/`,
  `MSL_C.PPCEABI.bare.H/`, `MSL_C/`, `ReconfigBATs.c`, `executor.c`.
- `libultra/` (all) — replaced by `os*` shims in `pc_os.c`/`pc_stubs.c`;
  `libc64/{malloc,sprintf,aprintf}.c`; `osreport.c`.
- `Famicom/` (PPC asm NES core) — replaced by `famicom_*` stubs.
- `src/game/m_card.c` — **replaced wholesale by `pc/src/pc_m_card.c`**. This
  is the only *game* source file the platform layer reimplements, and it is
  1442 LOC of save/village/travel logic, not a hardware shim.
- `AUS/`, `src/furniture/*` (folded into `f_furniture.c`), various
  `#include`-only `.c` fragments, `ef_effect_lib.c`,
  `jaudio_NES/internal/dsp_{cardunlock,GBAKey}.c`, `emu64_print.cpp`,
  `emu64_utility.c`, `jsyswrapper_{ext,main}.cpp`, `src/data/model/obj_e_boat/`.
- `src/main.c`'s `main()` → `ac_entry` and `src/static/boot.c`'s `main()` →
  `boot_main` via `-Dmain=…`. The platform layer owns the real `main()`.

### The platform surface is NOT only `pc/`

**84** non-data, non-furniture `.c`/`.cpp` files under `src/` carry
`#ifdef TARGET_PC` / `#ifndef TARGET_PC` conditional code (2681 files
including headers). The biggest and most load-bearing is
`src/static/jaudio_NES/internal/audiothread.c`, where lines 19–110 are a
complete TARGET_PC replacement for the GC threading model — it is why
`OSCreateThread`, `OSSetCurrentContext`, `__OSSetInterruptHandler`,
`OSSleepThread`, `OSExitThread` and friends are *called* in the source but
never *needed* at link time. `dc/` inherits this: the DC port either keeps
`TARGET_PC` defined (and lives with a single-threaded audio model) or
introduces `TARGET_DC` and re-opens all 84 files. **Recommendation: keep
`TARGET_PC` defined for M1 so the link closes, and add `TARGET_DC` only
where DC needs to differ.**

---

## 2. Which `pc/` files can be reused, and how much work each is

"Verbatim" below means *the `.c` body needs no edits*. Six of these still
`#include "pc_platform.h"`, which today pulls in `SDL.h` and `glad`; `dc/`
must supply an equivalent header (types, globals, small prototypes) before
they compile. That header is the single highest-leverage first file to write.

### 2a. Compile VERBATIM (body unchanged)

| file | LOC | why it survives | caveat |
|---|---|---|---|
| `pc_save_bswap.c` | 1029 | GCI byte-swap tables; SH-4 is LE exactly like the base targets | includes only decomp headers + `<string.h>` — **truly verbatim** |
| `pc_stubs.c` | 115 | pure empty stubs over `types.h` | **truly verbatim** (no `pc_platform.h`) |
| `pc_stubs_cpp.cpp` | 34 | JSystem C++ vtable bodies the decomp declares but never defines | needs `pc_platform.h` replacement only for types |
| `pc_mtx.c` | 571 | `PSMTX*`/`C_MTX*`/`gu*` in scalar C | correct as-is; §4.5 wants FTRV/FIPR rewrites of ~6 hot functions **later** |
| `pc_misc.c` | 222 | register-array dummies, PPC/EXI/SI stubs, `sins`/`coss`, malloc-arena bookkeeping | one `fprintf`; `bzero`/`bcopy` are `#ifdef _WIN32` (newlib provides them — *unverified for KOS*) |
| `pc_aram.c` | 81 | compiles unchanged | **but semantically wrong on DC** — 16 MB `malloc` cannot exist; see §4.3 |
| `pc_assets.c` | 30677 | generated dispatch table, plain C | its data source changes (ISO → `/cd`), the table does not; regenerate with `pc/tools/gen_runtime_assets.py` |

### 2b. Small edits (swap one backend, keep the logic)

| file | LOC | edit |
|---|---|---|
| `pc_prof.c` | 40 | `clock_gettime(CLOCK_MONOTONIC)` → KOS timer; `getenv` → compile-time flag (*KOS `clock_gettime` support unverified*) |
| `pc_dvd.c` | 247 | `fopen/fseek/fread` → KOS VFS `/cd`; add the read-ahead thread. Entry-table interning and the `DVDFileInfo` +0x18/+0x30/+0x34 layout stay |
| `pc_card.c` | 393 | file-per-GCI backend → VMU (`vmufs`); the 29 `CARD*` entry points and the 20-byte `CARDFileInfo` layout stay |
| `pc_m_card.c` | 1442 | pure game logic above `CARD*`; only the save-size/compression policy changes |
| `pc_settings.c` | 472 | drop SDL window calls; keep the ini parser or replace with compile-time defaults. `pc_settings_cull_limit_xz` is called from game code |
| `pc_vi.c` | 248 | frame-pacing/dynamic-FPS logic is portable; swap + retrace become `pvr_scene_finish()`/`vid_waitvbl` |
| `pc_audio.c` | 214 | ring buffer + `AI*`/`DSP*` surface stays; SDL device → KOS `snd_stream` |
| `pc_disc.c` | 445 | **moves to `tools/`** as host code — same source, different build |

### 2c. Rewrite from scratch

| file | LOC | why |
|---|---|---|
| `pc_gx.c` | 2685 | GL half dies. Portable half (batch merge, state dedup, strip/fan→list conversion, whole-batch frustum cull) transplants into the new backend |
| `pc_gx_texture.c` | 1227 | decoders survive; output stage becomes twiddled 16-bit / paletted / VQ in VRAM instead of linear RGBA8 |
| `pc_gx_tev.c` | 897 | **deleted** — TEV→GLSL generator, shader cache, shader seed. No shaders on PVR |
| `pc_main.c` | 619 | SDL/GL init, signal-based crash recovery → KOS init + SH-4 exception handler |
| `pc_pad.c` | 308 | SDL GameController → maple |
| `pc_os.c` | 495 | mmap arena → static arena; cache ops become real; address translation becomes real |
| `pc_overlay.c` | 1247 | GL overlay renderer — **drop** for now |
| `pc_texture_pack.c` | 1239 | HD texture packs — **drop** (non-goal) |
| `pc_model_viewer.c` | 891 | dev tool — **drop** |
| `pc_typing.c` / `pc_keybindings.c` | 148 / 255 | SDL text input → DC keyboard via maple; **drop** for M1 |
| `glibc_compat.c` | 34 | glibc symbol-version shims — **delete** (newlib) |

---

## 4. Top semantic landmines (ranked)

1. **`OSPhysicalToCached(0) + 0x28`** — the JKRHeap memory-size word. Get the
   physical-address mapping wrong and every heap in the game is created with
   a garbage size, at boot, silently. §3.1.
2. **Cache ops are real on SH-4.** 66 call sites currently compiled to
   nothing, several of them directly on buffers the PVR and AICA will read
   (`emu64.c:965/1004`, `aictrl.c` DAC buffers, `JKRAramPiece` DMA
   completion). Invalidate is line-granular and discards dirty lines. §3.2.
3. **`ARStartDMA` is the whole 16 MB ARAM problem in one function** — plus an
   argument-order swap in `ARQPostRequest` and two defensive behaviours
   (pointer normalisation, zero-fill on OOB) that callers rely on. §3.4.
4. **`GXEnd` is never called and `GXPosition*` carries color0 forward.** Any
   PVR vertex path that assumes a conventional begin/end or a fully-reset
   vertex will produce geometry that is subtly wrong rather than obviously
   broken. §3.6.
5. **`DVDReadAsyncPrio` fires its callback before returning.** The read-ahead
   thread that CD-R streaming requires must not turn this into a real async
   API without auditing every caller. §3.5.

Runners-up: `OSGetTick` wraps every ~106 s and the tick rate (40.5 MHz) is
baked into game timing; `OSGetCurrentThread()`'s result is dereferenced in
`boot.c`; `DVDFastOpen`/`CARDFileInfo`/`GXTexObj` are all layout-critical
structs the *game* allocates.

---

## 5. Complete symbol manifest

Legend: **✱** = the symbol name appears nowhere in `src/` or `include/`
(i.e. it is `pc/`-internal plumbing, not part of the game↔platform contract).
`pc/ file` is where the definition lives today.

## 7. Known gaps / not verified in this pass

- **`bzero` / `bcopy`**: `pc_misc.c` only defines them under `_WIN32`
  (glibc supplies them otherwise). Whether KOS/newlib for sh-elf supplies
  both was **not verified**. `boot.c` calls both. If missing, add them to
  `dc/`.
- **KOS API names** used above (`dcache_flush_range`, `dcache_inval_range`,
  `rtc_unix_secs`, `timer_ns_gettime64`, `vmufs`, `snd_stream`,
  `pvr_scene_finish`) are from `kb/research-dreamcast.md` and general KOS
  knowledge; **none were checked against a KOS tree in this pass** (no
  toolchain installed yet). Confirm during M0.
- **`clock_gettime(CLOCK_MONOTONIC)`** availability under KOS/newlib is
  unverified (`pc_prof.c` needs it).
- The symbol extraction is a **column-0 definition parser**, not a compiler.
  It found 698 non-static definitions across 27 `.c`/`.cpp` files. Spot
  checks against `pc_os.c`, `pc_aram.c`, `pc_dvd.c`, `pc_vi.c`, `pc_audio.c`
  (read in full) matched. A handful of `static` functions declared with an
  unusual layout could in principle be misclassified as non-static; the
  reverse (missing a real export) was not observed but cannot be excluded
  without a real link.
- **Reference counting** ("✱") is a whole-word identifier match over `src/`
  and `include/` including comments and `#ifdef`-disabled code. It over-
  counts (a symbol named in a dead `#else` branch counts as referenced) and
  is therefore a *safe* signal: ✱ genuinely means "no mention anywhere".
- `EXI*`, `SI*`, `DB*`, and most `PPC*` are marked `drop` because a grep over
  compiled `src/` found **0** call sites. The two live PPC paths are
  `PPCMfmsr`/`PPCMtmsr` (`JUTException.cpp`, MSR interrupt masking) and
  `PPCSync` (`emu64.c:5912`, `dspdriver.c:373`). Dropping the rest is safe
  only if nothing in `src/data/` references them — that subtree was included
  in the reference scan, so this is believed safe.
- The DC disposition column is a **design proposal**, not a measurement. It
  is the recommendation of this recon pass and should be revised as M1/M2
  measurements come in.
