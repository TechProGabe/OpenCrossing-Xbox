# State

**Short by design.** What is true now + next action.

## Where the port is (2026-09-27)

- M0 done: repo bootstrapped from upstream PC port v0.9.3 (`4099d246`), with
  ac-decomp head (`09ca8e8b`) merged in. Docs + reference kb imported.
- Nothing builds for Xbox yet. `xbox/` does not exist.

## Next action

M1: nxdk Docker image, `xbox/` CMake/Makefile skeleton, hello XBE in xemu,
then compile all of `src/` against nxdk headers (no link) and triage errors
into the prelude.

## `src/` TARGET_XBOX branch ledger

None yet. Every branch added must be listed here with file, symbol, reason.

## Closed (do not re-propose)

- 64-bit build — decomp assumes 32-bit pointers everywhere; Xbox is 32-bit anyway.
- GLSL/shader-cache path from `pc_gx_tev.c` — no GLSL compiler on nxdk.
