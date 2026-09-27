# Audio — the jaudio_NES engine and the shipped bank data

§1 and §3 of `kb/audio-plan.md`, moved verbatim: what the engine actually is
(an N64 libaudio driver, *not* JAudio/IBNK), its shipped parameters, and the
measured layout, encoding and size of `audiorom.img`. Read before touching bank
conversion, sample residency, or anything that assumes the format.
**Parent verdict, unchanged: audio is a real risk, not a solved problem**
(`kb/audio-plan.md`). Section numbers and [measured]/[modelled] tags are original.

---

## 1. What the engine actually is

`src/static/jaudio_NES/` is **not** JAudio/IBNK despite the `Bank_Test`/`IBNK`
code in `bankread.c` (that path is unused by GAFE01 — see §3). The live engine
is the **N64 libaudio synthesis driver of the Zelda MM / SM64 lineage**,
retargeted to GameCube, with the RSP audio microcode replaced by a C
interpreter (`internal/rspsim.c`).

Call chain, per audio frame:

```
Jac_VframeWork → MixCpu (cpubuf.c)  → Neos_Update (neosthread.c)
  → CreateAudioTask (sub_sys.c)     → Nas_smzAudioFrame (driver.c:171)
        ├─ Nas_MySeqMain(u)         ×4   sequencer (track.c/jammain_2.c) — cheap
        ├─ __Nas_PushDrvReg(u)      ×4   snapshot voice state → AG.common_channel[]
        └─ Nas_DriveRsp(...)        ×4   emit RSP command list  ← THE SEAM
              └─ Nas_SynthMain(...) ×voices  per-voice command emission
  → RspStart2/RspStart (rspsim.c)   interpret the command list = ALL the DSP work
  → AIInitDMA(dac, DAC_SIZE*2)      (aictrl.c) → pc_audio.c ring → SDL
```

### Shipped parameters **[measured]**

Source: `internal/rate.c`, `internal/audioconst.c` (`NA_SPEC_CONFIG[0]`),
`internal/memory.c:__Nas_MemoryReconfig`, `include/jaudio_NES/system.h`.

| parameter | value | where |
|---|---|---|
| output DAC rate | 32,028.5 Hz stereo | `JAC_DAC_RATE` |
| output samples / audio frame | 560 stereo pairs (`DAC_SIZE`=1120 s16) | `JAC_FRAMESAMPLES` |
| audio frame rate | 32028.5 / 560 = **57.19 Hz** | derived |
| internal mixing rate (`spec->_00`) | 0xBB80 = 48,000 nominal | `NA_SPEC_CONFIG` |
| samples/frame target | 48000 / 60 = 800 | `refresh_rate`=`REFRESH_RATE_NTSC`=60 |
| updates per frame | 4 | hardcoded in `__Nas_MemoryReconfig` |
| samples per update | (800/4) & ~7 = **200** | `num_samples_per_update` |
| **effective internal rate** | 4 × 200 × 57.19 = **45,755 Hz** | derived |
| **max simultaneous voices** | **24** (`spec->_05`, `AGC.maxChan`=0x18) | `AG.num_channels` |
| synth reverbs | 1 (`spec->_09`), `NA_HALL_DELAY` | `AG.num_synth_reverbs` |
| reverb delay line | `_02`=0x20 → 0x20×0x40 = **2048 samples** (8 KB L+R) | `Nas_SetDelayLineParam` case 1 |
| reverb sub-delay | 20 (non-zero ⇒ second load/save/mix path active) | `NA_HALL_DELAY.sub_delay` |
| default sound mode | `SOUND_OUTPUT_STEREO` | `system.c:1526` |
| max ABI cmds | 24×20×4 + 1×30 + 400 = 2350 | `AG.max_audio_cmds` |
| DMEM | 4 KB static (`rspsim.c:DMEM[0x1000]`) | fits SH-4's 16 KB D-cache |

Note the final resampler: `A_CMD_INTERLEAVE` calls `Jac_Resample16` which
converts each update's 200 internal samples → `JAC_FRAMESAMPLES>>2` = 140
output stereo pairs. So the engine mixes at ~45.75 kHz and downsamples to
32.03 kHz on the way out.

### Two effects are already off by default **[measured]**

- **Haas** (`Nas_Synth_Delay`) is gated on `AG.sound_mode == SOUND_OUTPUT_HEADSET`
  (`channel.c:45-53`). Default is STEREO ⇒ **off**.
- **Dolby surround** (`Nas_DolbySurround`) is gated on
  `AG.sound_mode == SOUND_OUTPUT_DOLBY_SURROUND` (`driver.c:1221`). Default
  STEREO ⇒ **off**.

Both are user-selectable in AC's options menu. On DC we lock the mode to
STEREO and delete both paths at zero cost relative to the default GC
experience. That removes two of the four "AC is heavier than SM64" items
for free.

Two more commands are **already no-ops in this port**: `A_CMD_UNK3`
(velocity convolution, GC microcode unknown) and `A_CMD_DISTFILTER`
(`rspsim.c:315, 395`). They cost nothing today and must not be "restored".

### No streamed audio **[measured]**

`internal/streamctrl.c` expects `/stream00.adp`…`/stream07.adp`. The GAFE01
FST contains **10 files, none of them `.adp`** (see `tools/dcasset/README.md`).
The disc-streaming subsystem is dead code in this game — **all** audio is
sequenced. There is no separate music-streaming port task.

---

## 3. Bank format and size — all **[measured]**

`audiorom.img` was extracted from the ISO at offset `0x000FE588`, 8,300,384
bytes, and parsed against `AudiodataHeaderStart` / `AudiobankHeaderStart` /
`AudiowaveHeaderStart` / `AudioseqHeaderStart` / `AudiomapHeaderStart` in
`src/static/jaudio_NES/game/audioheaders.c`.

### 3.1 Layout — three concatenated N64-style regions

`AudiodataHeaderStart` gives exactly three extents whose sizes **sum to the
file size to the byte**:

| region | offset | size | share |
|---|---|---:|---:|
| **Audioseq** (sequences) | 0x000000 | 849,664 | 10.2% |
| **Audiobank** (soundfont metadata) | 0x0CF700 | 425,088 | 5.1% |
| **Audiotable** (sample data) | 0x137380 | **7,025,632** | **84.6%** |

There is no container magic, no compression, no JAudio/IBNK/WSYS/AAF chunk
anywhere in the file (scanned; zero hits). It is a raw N64 audio ROM image.

### 3.2 Sequences and soundfonts

- **249 sequences.** median 2,176 B, mean 3,412 B, max 120,192 B.
  243 `CACHE_LOAD_TEMPORARY`, 4 persistent, 2 permanent.
- **159 soundfont entries** (154 non-empty). Metadata median 2,144 B, max
  20,736 B. Standard Zelda layout: `{Drum** drums; SoundEffect* sfx;
  Instrument* instruments[nInst];}`, with counts packed into the `ArcEntry`
  params exactly as `__SetVlute` reads them (`param0` = waveBankId0<<8|Id1,
  `param1` = nInst<<8|nDrums, `param2` = nSfx).
- **6 sample banks** (Audiotable entries): 239,936 / 57,088 / 434,304 /
  1,189,600 / 986,656 / **4,118,048** bytes.
- **Sequence→soundfont map** (`AudiomapHeaderStart`, 504 u16 = 1008 B):
  **247 of 249 sequences reference exactly one soundfont.** One references 2,
  one references 4.

### 3.3 Sample encoding

**All 1157 unique samples are `CODEC_ADPCM` = N64 VADPCM**, `medium = RAM`,
`order = 2`, `n_predictors = 2` (64-byte codebook each), 9 bytes per 16-sample
frame (4.5 bits/sample), 16-bit output. No S8, no S16, no SMALL_ADPCM, no
CODEC_REVERB in the shipped data — the multi-codec branches in
`Nas_SynthMain` are dead weight on this title.

- referenced sample bytes: **6,734,836** of the 7,025,632 Audiotable (95.9%;
  the rest is alignment/unreferenced)
- decoded PCM16 sample count: **≈ 11.97 M samples** (≈ 24 MB as PCM16)
- decoded length: median 5,312 samples, p90 21,040, **max 225,280**
- loop field: 451 samples loop forever (`count = 0xFFFFFFFF`), 706 are
  one-shot (`count = 0`). **No sample uses `count == 2`**, so the
  `stop_loop`/`sample_end` branch in `Nas_SynthMain` is dead.
- tuning floats: min 0.036, median 0.689, max 8.24 (drives pitch/decode ratio)

### 3.4 Converted to AICA 4-bit Yamaha ADPCM

4 bits/sample vs VADPCM's 4.5 ⇒ **× 8/9 = 0.889** of the source bytes.

| set | VADPCM | AICA 4-bit |
|---|---:|---:|
| **everything** | 6,734,836 | **5,986,040 (5.71 MB)** — does *not* fit 2 MB |
| wave bank 5 alone | 4,113,838 | 3,656,745 (3.49 MB) |
| **largest single soundfont** (bank 153, 126 instruments) | 2,217,394 | **1,971,016 (1.88 MB)** |
| **largest single sequence's set** (seq 242, banks {2,155,154,153}) | 3,844,022 | **3,416,908 (3.26 MB)** |
| second-largest sequence (seq 247, bank 153) | 2,217,394 | 1,971,016 (1.88 MB) |
| **median sequence** | 56,960 | **≈ 49 KB** |
| mean sequence | 83,601 | ≈ 72 KB |

**This is the decisive finding.** The 8.3 MB headline number is irrelevant.
The *resident* working set is per-sequence, and **the median sequence needs
49 KB of AICA sound RAM**. Only two sequences (242 and 247, the big
multi-bank music cues) approach or exceed the ~1.8 MB usable sound RAM.

⚠️ **CORRECTED 2026-08-05 — this paragraph used to end "No individual soundfont
exceeds 2 MB", and that is true of 2 MB and false of the USABLE sound RAM.** KOS
reserves **196,608 B** (`AICA_RAM_START 0x030000`, `aica_cmd_iface.h:37-38`),
leaving **1,900,544 B**. The largest soundfont, **bank 153, transcodes to
1,971,016 B — over by 70,472 B.** It fits only if `snd_stream` is retired and
the channels are driven directly. Treat "the largest soundfont fits" as
conditional on that decision, not as a property of the data.

**⇒ Answer to "convert offline or stream from disc": convert offline, page
per-sequence into sound RAM, and handle the two outliers specially.** No
per-sample disc streaming is needed for the common case.

### 3.5 The AICA constraints that bite

1. **65,536-sample per-channel limit.** 24 samples decode to more than 65,536
   samples (total 1,438,238 VADPCM bytes, mostly wave bank 5). These must be
   split across chained channels, converted to a CPU-fed ring, or
   downsampled below the limit. **[measured]**
2. **AICA ADPCM is stateful (Yamaha step-index)**, unlike VADPCM which resets
   its predictor state per 16-sample frame *and* carries an explicit
   `predictor_state[16]` at the loop point. A loop that jumps back into the
   middle of an AICA ADPCM stream produces a step-index discontinuity → audible
   click or slow drift. 451 samples loop. Mitigations, in order: (a) encode
   loop-carrying samples so the encoder state converges at `loop_end` ≈
   `loop_start` (dcaconv/wav2adpcm territory), (b) store looping samples as
   8-bit PCM (2× the bytes — still fits given §3.4), (c) rotate the sample so
   the loop starts at a block boundary. **This is the main stage-B risk.**
3. **1,900,544 B usable** of the 2,097,152 B. ⚠️ **Sharpened 2026-08-05 — this
   said "~1.8 MB … reserve figure is an estimate".** KOS's reserve is
   **196,608 B**, read out of the pinned tree: `AICA_RAM_START 0x030000`
   (`aica_cmd_iface.h:37-38`). That is the ARM driver's region only —
   `snd_stream` buffers come out of the remainder, so retiring `snd_stream` and
   driving channels directly is what buys the last ~70 KB bank 153 needs (§3.4).
