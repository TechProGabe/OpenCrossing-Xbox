# CLAUDE.md

OpenCrossing-Xbox is a native original Xbox (2001) port of Animal Crossing
(GameCube, GAFE01 USA Rev 0), built on the ACreTeam decomp and the
flyngmt/ACGC-PC-Port platform layer. Toolchain: nxdk. Target: retail Xbox,
stock 64 MB. It runs on real hardware; bugs are tracked in
`docs/known-issues.md`.

This file is an index plus the hard rules. Load `docs/` files on demand: read
the one the table points at, never the whole tree.

## Hard rules

- **Stock 64 MB.** 128 MB (mod / xemu setting) is a dev crutch, never a requirement.
- **Never edit `src/` to make it compile.** Compat goes in the Xbox prelude
  (force-included) or `xbox/`. Every `#if defined(TARGET_XBOX)` in `src/` and
  every `pc/` edit must be listed in `docs/patches.md` with its reason.
- **`-DTARGET_PC` stays.** It means "not GameCube" and guards the LE/32-bit
  fixes. `-DTARGET_XBOX` goes alongside it.
- **Keep `-fno-strict-aliasing -fwrapv`.** The decomp depends on both.
- **`pc/` is reference, `xbox/` is the build target.** Keep `pc/` close to
  upstream so `upstream-pc` cherry-picks apply; only real bug fixes go there.
- **Never commit ROM material** (`.iso` `.gcm` `.ciso` `.gci`, extracted assets,
  saves, built XBE/ISO images with game data). Users supply their own disc image.
- **Every optimization gets a kill switch; the default is the good build.**
- **xemu is not hardware.** Timing, cache and memory-pressure claims get judged
  on a real Xbox.
- **Branches:** work on `dev`. `main` is the release branch: every push to it
  publishes a public beta (`docs/toolchain.md`). Subagents do not run git; the
  main thread commits.

## Doc map

| file | contents |
|---|---|
| `docs/known-issues.md` | open bugs with leads, what's not ported |
| `docs/architecture.md` | base, hardware compared, frame path, seams, files on the console, decisions |
| `docs/traps.md` | gotchas; read before touching build, arena, prelude, audio |
| `docs/patches.md` | every change to `src/`, `include/`, `pc/` and why |
| `docs/toolchain.md` | build, xemu, hardware testing + logs, branches, releases |
| `docs/renderer.md` | GX → NV2A: GL shim, vertex program, combiners, debug knobs |
| `docs/memory.md` | 64 MB budget and measurements |
| `docs/perf.md` | optimization method and measurements |
| `docs/backport.md` | what came back from Melee-X, and what's left |
| `docs/upstream.md` | remotes, base SHAs, sync policy |
| `docs/releases/` | release notes, one file per full release (`vX.md`, used by CI) |
| `docs/ref/README.md` | imported reference kb (DC + Anbernic): what applies |
| `docs/decomp/` | upstream decomp onboarding (Ghidra, m2c, decomp.me) |
| `pc/DOCUMENTATION.md` | PC port architecture (reference) |

## Keeping this current

Settled fact → the matching `docs/` file. Rejected idea → "Decided against"
in `docs/architecture.md`. Bug found → `docs/known-issues.md`; delete it when
fixed. Gotcha → `docs/traps.md`. Add/remove a doc → update the map. Cite
symbols, not line numbers. Treat unsourced numbers in `docs/ref/` as claims.
