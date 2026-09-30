# OpenCrossing-Xbox

OpenCrossing-Xbox runs Animal Crossing (GameCube, USA) natively on an original Xbox. It is not an emulator: the decompiled game code is compiled for the Xbox's Pentium III and draws with its NV2A GPU. It runs at 60 fps on a stock 64 MB console, at up to 720p.

- 480i, 480p or 720p output. 720p needs a component cable.
- 4:3 or 16:9 widescreen.
- An Options menu, on the title screen and in the pause menu, for video, audio, controls (including button remapping) and gameplay settings.
- Saves use the GameCube `.gci` format, so a town can move between the Xbox, Dolphin and the PC port.

Status: beta. The game is mostly playable, but it still crashes now and then and has bugs (see [Known issues](#known-issues)). Save often.

You need your own copy of the game. This project ships no game data.

Other OpenCrossing ports: [OpenCrossing-Anbernic](https://github.com/GabeConway/OpenCrossing-Anbernic) (H700 handhelds) and [OpenCrossing-Dreamcast](https://github.com/GabeConway/OpenCrossing-Dreamcast).

## What you need

- A modded original Xbox (softmod or modchip) that can run homebrew. On a HDD install you also need a way to copy files to it, usually FTP from your dashboard (UnleashX, XBMC4Xbox, EvolutionX and others include a server).
- A controller. The Duke and the Controller S both work.
- A disc image of Animal Crossing for GameCube, USA version (game ID GAFE01, Rev 0), dumped from your own disc. `.iso`, `.gcm` and `.ciso` all work, with any filename. Other regions and Rev 1 are not supported.

You can also play it in [xemu](https://xemu.app) (see below).

## Download

Go to the [Releases](../../releases) page and download the newest `OpenCrossing-Xbox-beta-<n>.zip`. Every build is a beta for now. Unzip it. You get:

```
OpenCrossing/
  default.xbe      the game
  default.tbn      dashboard icon
tools/
  make-xiso        packs the game and your disc image into a burnable ISO
  gc_trim_ciso.py  shrinks your disc image to the ~28 MB the game reads
  gcs_to_gci.py    converts a GameCube save export for use on the Xbox
```

Then pick one of the three ways to play.

## Option 1: install to the Xbox hard drive

This is the easiest way, and loading is fastest.

1. Put your disc image in the `OpenCrossing` folder, next to `default.xbe`. It can have any name.
2. Connect to your Xbox with an FTP client such as FileZilla. Your dashboard shows the Xbox's IP address; the default login on most dashboards is user `xbox`, password `xbox`.
3. Copy the whole `OpenCrossing` folder to wherever your dashboard looks for games or applications, for example `E:\Games\` or `F:\Applications\`.
4. Launch OpenCrossing from the dashboard. You should see the leaf icon.

The full 1.4 GB disc image works fine. To save space, shrink it first with `python3 tools/gc_trim_ciso.py "Animal Crossing.iso" AnimalCrossing.ciso` and copy the `.ciso` instead (about 28 MB).

```
E:\Games\OpenCrossing\
  default.xbe
  default.tbn
  Animal Crossing.iso    (yours)
```

## Option 2: burn a disc

`make-xiso` packs `default.xbe` and your disc image into one Xbox ISO. It trims the image first, so the result is about 35 MB and fits on a CD-R.

1. Install the requirements: `bash`, `python3` and [xdvdfs](https://github.com/antangelo/xdvdfs) (download a release binary or run `cargo install xdvdfs-cli`, and make sure `xdvdfs` is on your PATH). On Windows, run the script from WSL or Git Bash.
2. Run:

   ```sh
   tools/make-xiso OpenCrossing/default.xbe "Animal Crossing.iso" OpenCrossing.iso
   ```

   Add `--full` before the XBE path to keep the untrimmed image (about 1.5 GB, needs a DVD-R).
3. Burn `OpenCrossing.iso` to a CD-R or DVD-R at a low speed (for example with ImgBurn), as a plain image, without converting it.
4. Put the disc in your modded Xbox and launch it from the dashboard's disc option.

Not every Xbox DVD drive reads burned discs. Samsung drives are usually the most forgiving, Thomson drives the least. If yours won't read the disc, use Option 1.

## Option 3: xemu

Build the ISO as in Option 2 and load it in xemu with Machine > Load Disc. Set xemu's memory to 64 MB. Sound works, but your saves go to xemu's virtual hard drive.

## Saves and settings

Saves go to the hard drive in every mode, including discs, at `E:\UDATA\4f430001\save\card_a\`. They use the GameCube `.gci` format. You can move them back to Dolphin, or to the [PC port](https://github.com/flyngmt/ACGC-PC-Port).

To continue a town from your GameCube or Dolphin:

1. Export the save. Dolphin's memory card manager gives you a `.gci`. GameShark and GC Memcard Manager exports are `.gcs`.
2. Convert it: `python3 tools/gcs_to_gci.py mysave.gcs`. This writes `DobutsunomoriP_MURA.gci`. A `.gci` from Dolphin needs no conversion.
3. Name it exactly `DobutsunomoriP_MURA.gci`. Dolphin names its exports `01-GAFE-DobutsunomoriP_MURA.gci`, and other names aren't found yet.
4. Launch the game once so it creates its folders. Then FTP the `.gci` to `E:\UDATA\4f430001\save\card_a\`, replacing the file there. If you already started a town on the Xbox, copy its `DobutsunomoriP_MURA.gci` off first: the file of that name is the one that loads.

Settings are in **Options** on the title screen, or **Settings** in the pause menu (Back button):

| tab | settings |
|---|---|
| Video | Output (480i/480p, or 720p with a component cable and 720p enabled in the dashboard; needs a restart), Widescreen (4:3, 16:9, or Auto to follow the dashboard), texture filter |
| Audio | master volume |
| Controls | left and right stick dead zones, rumble strength, button remapping |
| Gameplay | Mr. Resetti, shop upgrade (Singleplayer lets Nook upgrade to Nookington's without a visitor from another town), borderless acres (Off brings back the original acre-by-acre camera), NES aspect |

They are saved to `E:\UDATA\4f430001\settings.ini`, which you can also edit over FTP.

**Quit Game** on the title screen or in the pause menu goes back to the dashboard.

## Controls

| Xbox | GameCube |
|---|---|
| A, B, X, Y | A, B, X, Y |
| Start | Start |
| Black button | Z |
| Left / right trigger | L / R |
| Left stick | control stick |
| Right stick | C-stick |
| D-pad | D-pad |
| Back | pause menu |

Buttons can be remapped in Options > Controls > Buttons.

The left stick's dead zone defaults to 43%, which suits a worn controller whose stick doesn't return to the centre. If your controller is in good shape, lower it to 15-20% in Options > Controls so small pushes register sooner; the page shows how far the stick is tilted right now, so set the dead zone just above where yours rests.

## Known issues

- The left stick has a large dead zone (43%) by default, tuned on a worn controller. Lower it in Options > Controls (see Controls).
- NES games run a little choppy.
- A save only loads if it is named `DobutsunomoriP_MURA.gci` (see Saves and settings).

The full list with technical detail is in [docs/known-issues.md](docs/known-issues.md). When you report a crash or freeze, include `crash.log`, `last.log`, `hang.log`, `perf.log` and `boot.log` from `E:\UDATA\4f430001\` if they exist. If the screen shows a crash report, a photo of it helps too.

## Building from source

You need Docker, Python 3 and Pillow.

```sh
xbox/build-image.sh   # once: builds the nxdk SDK image, about 10 minutes
xbox/build.sh         # writes build-xbox/xbe/default.xbe
```

See [docs/toolchain.md](docs/toolchain.md) for testing in xemu and on hardware, and [docs/architecture.md](docs/architecture.md) for how the port works. Development happens on the `dev` branch; `main` is released as a beta on every push.

## Legal

This repository contains no game assets, no ROM data and no Nintendo code. It holds the decompiled C source (CC0, ACreTeam) and port code (MIT). You need your own legally obtained copy of Animal Crossing. Do not open issues asking for ROMs.

Not affiliated with or endorsed by Nintendo or Microsoft. Animal Crossing is a trademark of Nintendo. Xbox is a trademark of Microsoft.

## Credits

- [ACreTeam/ac-decomp](https://github.com/ACreTeam/ac-decomp), the decompilation this is built on.
- [flyngmt/ACGC-PC-Port](https://github.com/flyngmt/ACGC-PC-Port) and its contributors, for the PC port this repo descends from (GX to GL layer, runtime disc reader, frame-rate fixes, NES integration).
- [XboxDev/nxdk](https://github.com/XboxDev/nxdk), [xemu](https://xemu.app) and [xdvdfs](https://github.com/antangelo/xdvdfs): the open Xbox toolchain, emulator and ISO packer.
- AI tools (Claude) were used in developing this port.

See [LICENSE](LICENSE).
