# Platform API surface — what `dc/` must provide

Recon pass, 2026-08-01. Derived by reading `pc/CMakeLists.txt`, every
`pc/src/*.c` and `pc/include/*.h`, and cross-checking against call sites in
the vendored `src/` and `include/`. **Nothing here is guessed** — every symbol
below is a non-static definition that exists today in `pc/src/`; every
"N call sites" figure is a grep over the compiled (non-excluded) subset of
`src/`. Where something could not be verified it says so.

Companion docs: `PLAN.md` (§3 the four hard problems), `kb/base-repo-map.md`
(what the base repo contains), `kb/research-dreamcast.md` (KOS/PVR facts).

---

**This file is now an index.** The 698-symbol manifest was split by subsystem
so an agent can load the one file it needs. Content was moved verbatim; the
old section numbers are kept in the table so existing citations still resolve.

| file | old §§ | what it answers |
|---|---|---|
| `kb/platform-api-overview.md` | §0, §2, §4, §5 legend, §7 | symbol counts and dispositions, which `pc/` files survive, the ranked landmines, what is unverified. **Read this first — it carries the table legend.** |
| `kb/platform-api-boot-order.md` | §1 | the verified init order and the six hard ordering rules |
| `kb/platform-api-os-core.md` | §3.1–3.3, §3.7, §5 | arena / `+0x28` word, cache maintenance, time & ticks, threads; the `OS*` tables |
| `kb/platform-api-os-stubs.md` | §5 | REL, libc64 malloc, N64 trig, PPC, EXI/SI/DB, libc, libultra, GBA, Famicom, JSystem vtables, glibc compat |
| `kb/platform-api-math.md` | §5 | `PSMTX*` / `C_MTX*` / `gu*`, with the FTRV/FIPR candidates |
| `kb/platform-api-gx.md` | §3.6, §5 | the GX state machine and every `GX*` symbol, including textures and TLUTs |
| `kb/platform-api-vi-pad.md` | §5 | `VI*` frame pacing and `PAD*` input |
| `kb/platform-api-dvd-aram.md` | §3.4, §3.5, §5 | `DVDReadAsyncPrio`, `ARStartDMA`, the asset table, the disc reader |
| `kb/platform-api-audio.md` | §5 | `AI*` / `DSP*` — the `AIInitDMA` handoff point |
| `kb/platform-api-save-card.md` | §3.8, §5 | save layout, `CARD*`, `pc_m_card.c`, GCI byte-swap |
| `kb/platform-api-pc-only.md` | §5 | settings, profiler, overlay, texture packs — the `drop` pile |
| `kb/platform-api-globals.md` | §6 | global *variables* the platform layer must define |
