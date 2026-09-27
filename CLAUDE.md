# CLAUDE.md

**OpenCrossing-Xbox** — native original Xbox (2001) port of Animal Crossing
(GameCube, GAFE01 USA Rev 0), built on the ACreTeam decomp and the
flyngmt/ACGC-PC-Port platform layer. Toolchain: nxdk. Target: **retail Xbox,
stock 64 MB**. Dev loop: xemu until stable, then a modded Xbox over FTP.

**This file is an index + the hard rules.** Load `docs/` files on demand — read
the one the table points at, never the whole tree. No status narrative here:
status is `docs/STATE.md`.

## 1. Hard rules

- **Stock 64 MB.** 128 MB (mod / xemu setting) is a dev crutch, never a requirement.
- **Never edit `src/` to make it compile.** Compat goes in the Xbox prelude
  (force-included) or `xbox/`. Every `#if defined(TARGET_XBOX)` in `src/` must
  be listed in `docs/STATE.md` with its reason.
- **`-DTARGET_PC` stays.** It means "not GameCube" and guards the LE/32-bit
  fixes. `-DTARGET_XBOX` goes alongside it.
- **Keep `-fno-strict-aliasing -fwrapv`.** The decomp depends on both.
- **`pc/` is reference, `xbox/` is the build target.** Don't "fix" `pc/` for Xbox;
  keep it close to upstream so `upstream-pc` cherry-picks apply.
- **Never commit ROM material** (`.iso` `.gcm` `.ciso` `.gci`, extracted assets,
  built XBE/ISO images with game data). User supplies their own disc image.
- **Every optimization gets a kill switch; the default is the good build.**
- **xemu is not hardware.** Timing, cache, and memory-pressure claims get judged
  on a real Xbox once M7 starts.
- Branch: `main`. Subagents do not run git; the main thread commits.

## 2. Start here

| order | file | why |
|---|---|---|
| 0 | `docs/STATE.md` | where the port is, next action, `src/` branch ledger |
| 1 | `docs/PLAN.md` | milestones M0–M7, gates, risks |
| 2 | `docs/traps.md` | before touching build, arena, prelude |

## 3. Doc map

| file | contents |
|---|---|
| `docs/hardware.md` | Xbox hardware contract vs GC / DC |
| `docs/toolchain.md` | nxdk, Docker/colima, xemu, FTP deploy |
| `docs/upstream.md` | remotes, base SHAs, sync + conflict policy |
| `docs/renderer.md` | GX → NV2A plan: pbgl bring-up → xgu + register combiners |
| `docs/memory.md` | 64 MB budget, PC-port numbers, levers |
| `docs/ref/README.md` | imported reference kb (DC + Anbernic) — what applies, what doesn't |
| `docs/decomp/` | upstream decomp onboarding (Ghidra, m2c, decomp.me) |
| `pc/DOCUMENTATION.md` | PC port architecture (reference) |

## 4. Keeping this current

Settled fact → the matching `docs/` file. Dead idea → `docs/STATE.md` "closed"
list. Gotcha → `docs/traps.md`. Add/remove a doc → update §3. Cite symbols, not
line numbers (they drift). Treat unsourced numbers in `docs/ref/` as claims.
