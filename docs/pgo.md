# LTO and PGO

Two build-level optimizations from Melee-X (its `docs/pgo.md`), off by
default until the console measures them (`CLAUDE.md`: every optimization
gets a kill switch). Asked for on 2026-10-29 to steady 720p, where busy
scenes spend 10-12 ms a frame in the game and emu64 (`perf.md`).

| knob | what | folder |
|---|---|---|
| `XBOX_LTO=1` | ThinLTO over the whole image, in lld-link | `build-xbox-lto` |
| `XBOX_PGO=gen` | the instrumented build that records branch counts | `build-xbox-pgogen` |
| `XBOX_PGO=xbox/ac.profdata` | optimized with the profile | `build-xbox-pgo`, with LTO `build-xbox-lto-pgo` |

Both `xbox/build.sh` (Docker) and `tools/xbox/msys/build.sh` (Windows)
take them as environment variables; each kind builds in its own folder, so
switching never recompiles another kind's objects.

## ThinLTO

`-flto=thin` on every TU of `ac_xbox`; lld-link runs the ThinLTO backends
itself. Melee-X runs them by hand only to keep its link order file; we
have none. Cross-module imports are capped at 10 instructions
(`-mllvm:-import-instr-limit=10`): Melee-X measured LLVM's default (100)
growing `.text` by 30%, which the 16 KB code cache pays for, and 10 kept
most of the instruction savings for +46 KB. On its hardware LTO + PGO
together gave +11.6% fps.

## The profile (`xbox/ac.profdata`)

An LLVM indexed profile: per function its name, a hash of its control flow
and a count per instrumented block. No game data (names are the same
identifiers as in `src/`; counts are measurements of our own build), so it
can be committed with `xbox/ac.profdata.txt` saying where it came from.

A stale profile never changes behaviour, only speed: functions whose
control flow changed no longer match their hash and are compiled as if
unprofiled. Regenerate after big changes to hot code and before a release.

What must match between training and use: the LLVM version (21), the
compile flags, and `-mllvm -static-func-full-module-prefix=false` on both
sides (static functions keyed by file name, since the two builds' object
folders differ). Value profiling is off (`-mllvm -disable-vp`): the image
has no compiler-rt.

## Regenerating it

```sh
OCX_ISO=<GAFE01 image> OCX_CARD=<folder with a .gci town> \
OCX_XEMU=/c/xemu/xemu/xemu.exe OCX_XISO=/c/xdev/nxdk/tools/extract-xiso/build/extract-xiso.exe \
OCX_XEMU_ARGS="-config_path <64 MB xemu.toml, own HDD copy>" tools/xbox/pgo_train.sh
git add xbox/ac.profdata xbox/ac.profdata.txt
```

`tools/xbox/pgo_train.sh` builds the instrumented image (scripted pads,
saves from `D:\`, its own `E:\UDATA` folder, no full disc check), plays
`demo` (the title's attract demos), `newgame` (A / START presses through a
new town, the train, Rover and Nook) and `town` (the given save, then
walking in circles) in xemu, converts each run's last `[PGOC]` dump with
`tools/xbox/pgo_raw.py`, merges them with `llvm-profdata` and writes the
provenance file. Branch counts don't depend on xemu's speed. One xemu at a
time (`harness/xbox/run.sh` refuses to start beside another).

How it works: the instrumented image has no compiler-rt;
`xbox/src/xbox_pgo.c` defines `__llvm_profile_runtime` and writes the
counter section (`.lprfc`) as `[PGOC]` lines every 1800 frames
(cumulative since boot). `pgo_raw.py` rebuilds a raw profile (version 10,
32-bit) from the last complete dump plus the linked image's `.lprfd`
(function records) and `.lprfn` (names) sections and the
`__llvm_profile_raw_version` word, which the link keeps with `-include:`.

## Release builds

**Not shippable yet:** on the console the LTO + PGO build drew wrong colours
(boot screen and game; plain build right; `known-issues.md`, 2026-10-29).

Plain until a console round compares the two (perf.log's `min` lines in
the same busy rooms, both outputs). Then CI's `build.yml` gets
`XBOX_LTO=1 XBOX_PGO=xbox/ac.profdata`, and `build-xbox-lto-pgo` holds the
release's `ac_xbox.map` for `crash.log` reports.
