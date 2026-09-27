# Upstream sync

## Remotes

| remote | repo | role |
|---|---|---|
| `origin` | GabeConway/OpenCrossing-Xbox (private) | this repo |
| `upstream-pc` | flyngmt/ACGC-PC-Port | platform layer + game fixes. **History shared** — we are a descendant of its `main` |
| `decomp` | ACreTeam/ac-decomp | game source of truth. **No shared history** (PC port squashed its import) |

## Bases (2026-09-27)

- PC port: `4099d246` "Version 0.9.3" (2026-08-01).
- Decomp: PC port's import matches decomp **`c5215178`** (2025-11-06) — the last
  decomp commit before the PC port's first commit. Use it as the merge base for
  3-way merges of `src/` and `include/`.
- Decomp head merged: **`09ca8e8b`** (2026-07-17, "100%, link jaudio_NES/rspsim").

## How the decomp head was merged

34 files changed upstream since `c5215178` and differed from the PC port. Per-file
`git merge-file <pc> <c5215178> <decomp head>`:
- 25 merged clean. Net effect: 5 files (Sky/Air swing rename in USA code path,
  `ROOM_TYPE_*` enum, `*_8b_Dolphin`/`MultiBlock` GBI macros).
- **Kept PC side wholesale:** `src/static/jaudio_NES/**`, `include/jaudio_NES/**`,
  `src/static/Famicom/**`, `include/PR/gbi.h`, `include/PR/ultratypes.h`,
  `ac_npc_ctrl.c_inc`, `ac_npc_totakeke.c`. Upstream changes there are
  byte-matching refactors; the PC side carries LE fixes (`Nas_MapHeaderReadByte`),
  RSP precision fixes, fixNES in place of the PPC-asm NES core, and dt fixes.
- Dropped `include/gcc/`, `include/compiler/gcc/` (mwcc-build-only, gitignored).

## Next sync

```sh
git fetch upstream-pc decomp
git merge upstream-pc/main               # shared history: normal merge
# decomp: repeat the per-file 3-way merge with base = last merged decomp head
git diff --name-only 09ca8e8b decomp/master -- src include
```

Record the new decomp head here after each sync.
