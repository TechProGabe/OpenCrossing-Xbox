# Reference kb (imported)

Copied read-only from sibling ports. **Numbers and file paths refer to those
repos**, not this one — treat as claims until re-verified here.

| dir | source | commit |
|---|---|---|
| `dc/` | GabeConway/OpenCrossing-Dreamcast `kb/` | `4a9005f` (2026-08-15) |
| `anbernic/` | GabeConway/OpenCrossing-Anbernic `kb/` | `74a9998` (2026-07-29) |

## Applies to Xbox

| file | use |
|---|---|
| `dc/tev-map*.md` | TEV → fixed-function mapping; **`tev-map-table.md` is the M3 spec** |
| `dc/design-platform-api.md`, `dc/platform-api-*.md` | symbol-by-symbol map of `pc/src` — which seams to reimplement in `xbox/`. Start at `platform-api-overview.md`; `platform-api-boot-order.md` = ordering rules |
| `dc/upstream-pc-port.md` | what the upstream PC port does and does not give us |
| `dc/game.md`, `anbernic/game.md` | decomp/emu64/data facts, saves, settings |
| `dc/texture-path.md` | GC texture → GPU path |
| `dc/audio-engine.md` | audio engine is N64 libaudio-style, not JAudio; `audiorom.img` |
| `dc/save-layout.md` | save struct sizes, `.gci` layout |
| `dc/issues.md`, `anbernic/issues.md` | known game-side bugs with leads |
| `anbernic/renderer.md` | `pc_gx*` batching/culling/caches in depth |
| `anbernic/perf.md` | what already got optimized in `pc_gx` and how it was measured |

## Does not apply

DC's 16 MB levers (asset stubbing, texpool, src-shrink), VMU, AICA, PVR,
SH-4 cache/profiling work; Anbernic's GLES/Mali/PortMaster specifics.
