# Hardware contract

| | GameCube | PC port | Dreamcast (sibling) | **Xbox** |
|---|---|---|---|---|
| CPU | Gekko PPC 485 MHz | x86-32 | SH-4 200 MHz | **Pentium III-class 733 MHz, 128 KB L2, SSE1** |
| RAM | 24 MB + 16 MB ARAM | plenty | 16 MB | **64 MB unified (CPU + GPU)**; 128 MB mod/devkit |
| GPU | Flipper, TEV (≤16 stages) | GL 3.3 shaders | PVR, fixed, 1 TMU | **NV2A**: HW T&L + vertex programs, 4 textures/pass, 8 general + final register combiners |
| Endian | BE | LE | LE | **LE** |
| Pointers | 32-bit | 32-bit (enforced) | 32-bit | **32-bit** |
| Audio | DSP + ARAM | SDL + rspsim | AICA | **MCPX APU / AC97**; nxdk exposes AC97 + SDL audio |
| Storage | 1.46 GB disc | file | CD-R ~0.5 MB/s | **HDD (FATX)** + DVD `D:\` |
| Save | memcard ~456 KB | GCI file | VMU 100 KB | **HDD file** (keep GCI format) |
| Video | 640×480i | any | 640×480 | 480i/p, 720p/1080i possible |

## What this means

- ABI identical to the PC port → `src/` + most of `pc/` compile as-is.
- The TEV maps onto combiners far better than onto the DC PVR: a TEV stage is
  `d + (1-c)·a + c·b` (+bias, ×scale); a combiner stage computes `A·B + C·D` with
  input mappings (invert, expand, half-bias) and output scale/bias. The game
  uses ≤3 TEV stages (`PC_GX_MAX_TEV_STAGES`), well under 8.
- RAM is the one tight axis; everything else has headroom.
- Titles run from HDD, so the user's ISO sits next to `default.xbe`; no streaming
  concerns like DC's CD-R.
