<div align="center">

# 🍃 OpenCrossing-Xbox

**Animal Crossing (GameCube) running natively on the original Xbox.**
No emulator. The decompiled game code runs on the Xbox's Pentium III and draws with the NV2A.

![platform](https://img.shields.io/badge/platform-Original%20Xbox%20(2001)-107C10?style=flat-square)
![ram](https://img.shields.io/badge/RAM-stock%2064%20MB-2ea44f?style=flat-square)
![sdk](https://img.shields.io/badge/SDK-nxdk-blue?style=flat-square)
![status](https://img.shields.io/badge/status-early%20WIP-orange?style=flat-square)
![game](https://img.shields.io/badge/game-GAFE01%20USA%20Rev%200-lightgrey?style=flat-square)

</div>

---

## What this is

Nintendo's 2001 village sim, rebuilt from the [ACreTeam decompilation](https://github.com/ACreTeam/ac-decomp) and compiled as a native Xbox executable (`default.xbe`). It is the third [OpenCrossing](https://github.com/GabeConway) port:

| port | hardware | status |
|---|---|---|
| [OpenCrossing-Anbernic](https://github.com/GabeConway/OpenCrossing-Anbernic) | H700 handhelds, ~60 fps | playable |
| [OpenCrossing-Dreamcast](https://github.com/GabeConway/OpenCrossing-Dreamcast) | stock 16 MB Dreamcast | walks the town on real hardware |
| **OpenCrossing-Xbox** | stock 64 MB original Xbox | 🚧 bootstrapping |

As far as we know it is the first Animal Crossing port to the original Xbox. We searched GitHub and the web on 2026-09-27 and found none.

## How you'll play it (once released)

No compiling.

1. Download the release zip and unpack it. It contains a folder with `default.xbe` in it.
2. Copy that folder to your Xbox, e.g. `E:\Games\OpenCrossing\` (FTP from a softmodded/modchipped box, or into an xemu HDD image).
3. Put **your own** Animal Crossing disc image (`.iso`, `.gcm` or `.ciso`) in the same folder, next to `default.xbe`:

   ```
   E:\Games\OpenCrossing\
   ├── default.xbe
   └── Animal Crossing.iso     ← yours, any filename
   ```
4. Launch it from your dashboard.

**Prefer a disc?** Run the included `make-xiso` script on your computer. It packs `default.xbe` and your disc image into one XISO (trimmed to ~30 MB, so a CD-R is enough) you can burn to a CD-R or DVD-R (modded Xbox with a drive that reads it) or load in xemu with *Load Disc*. The script runs locally on your own files, and no game data ever comes from us.

The game reads its assets directly from your disc image at startup. There is no extraction step. Saves always go to the HDD (`E:\UDATA\`, the normal Xbox save location, even when booting from disc) in the GameCube `.gci` format, so they carry across the OpenCrossing ports and the PC port.

## How it works

```
 your disc image (.iso/.gcm/.ciso) ──► runtime disc reader (FST, Yaz0, DOL/REL assets)
                                              │
 decompiled game C ──► N64 display lists ──► emu64 (Nintendo's own N64→GX layer)
                                              │ GX calls
                                              ▼
                     pc_gx: batching · frustum cull · texture decode + cache
                                              │
                                              ▼
                     NV2A backend: fixed-function (pbgl) → register combiners (xgu)
```

The Xbox is a 32-bit little-endian x86 machine, which is the same ABI the upstream PC port already targets. The game logic, emu64, culling, texture decoders, disc reader and save code compile unchanged. The port work is in the seams: video, audio, input, memory, and translating the GameCube TEV into NV2A register combiners. A TEV stage maps onto a combiner stage almost one-to-one.

## Roadmap

- [x] **M0** Repo: PC-port base + latest decomp head, docs
- [x] **M1** nxdk toolchain, hello XBE in xemu, `src/` compiles
- [x] **M2** Boots headless, main loop runs
- [x] **M3** First pixels — title demo renders on the NV2A (vertex program + register combiners)
- [ ] **M4** Controller, music, saves
- [ ] **M5** Fits stock 64 MB
- [ ] **M6** Renderer fidelity (swap tables, indirect, EFB copies) + stable 30 fps
- [ ] **M7** Real hardware + first release (HDD folder + burnable XISO)

Details: [`docs/PLAN.md`](docs/PLAN.md) · status: [`docs/STATE.md`](docs/STATE.md)

## Building from source (developers)

`xbox/build-image.sh` (once) → `xbox/build.sh` → `build-xbox/xbe/default.xbe`; `harness/xbox/run.sh` runs it in xemu. See [`docs/toolchain.md`](docs/toolchain.md).

## Legal

This repo contains **no game assets, no ROM data and no Nintendo code**. It contains the decompiled C source (CC0, ACreTeam) and port code (MIT). You need your own legally obtained copy of *Animal Crossing* (GAFE01, USA Rev 0). Do not open issues asking for ROMs.

Not affiliated with or endorsed by Nintendo or Microsoft. *Animal Crossing* is a trademark of Nintendo. *Xbox* is a trademark of Microsoft.

## Credits

- **[ACreTeam](https://github.com/ACreTeam/ac-decomp)**: the 100% decompilation everything here stands on.
- **[flyngmt/ACGC-PC-Port](https://github.com/flyngmt/ACGC-PC-Port)** and its contributors: the PC port this repo descends from (GX→GL layer, runtime disc reader, dt fixes, fixNES integration).
- **[XboxDev/nxdk](https://github.com/XboxDev/nxdk)**, **[pbgl](https://github.com/fgsfdsfgs/pbgl)**, **[xemu](https://xemu.app)**, **[xdvdfs](https://github.com/antangelo/xdvdfs)**: the open Xbox toolchain, emulator and disc packer.
- AI tools (Claude) were used in developing this port.

See [LICENSE](LICENSE) (CC0 decomp + MIT port layer).
