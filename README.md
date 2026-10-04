# OpenCrossing-Xbox

Animal Crossing (GameCube, USA) running natively on an original Xbox. This isn't an emulator. The game's decompiled source is compiled for the Xbox's Pentium III and drawn by its own GPU, and it runs at 60 fps on a stock 64 MB console, at up to 720p.

What you get:

- 480i, 480p or 720p. The game picks the best mode your dashboard allows, and you can pin one yourself in Options.
- 4:3 or 16:9, following your dashboard's widescreen setting unless you pick one.
- An Options menu drawn in the game's own style, on the title screen and in the pause menu: video, audio, controls (with button remapping) and gameplay settings.
- The NES games in your house. All of them work except Clu Clu Land D.
- Saves in the GameCube `.gci` format, so your town moves between the Xbox, Dolphin and the PC port.
- Optional screenshots and an FPS counter.

This is version 1. The whole game plays, and it holds up for long sessions, but it's still a port of a decompiled game: expect the odd bug, and save often. See [Known issues](#known-issues).

You need your own copy of the game. Nothing from the game ships with this project.

There are other OpenCrossing ports too: [OpenCrossing-Anbernic](https://github.com/GabeConway/OpenCrossing-Anbernic) for H700 handhelds and [OpenCrossing-Dreamcast](https://github.com/GabeConway/OpenCrossing-Dreamcast).

## What you need

- An original Xbox that can run homebrew (softmod or modchip). For a hard drive install you also need a way to copy files to it. Most dashboards (UnleashX, XBMC4Xbox, EvolutionX and others) include an FTP server.
- A controller. The Duke and the Controller S both work.
- A disc image of Animal Crossing for GameCube, USA version (game ID GAFE01, Rev 0), dumped from your own disc. `.iso`, `.gcm` and `.ciso` all work, under any file name. Other regions and Rev 1 don't.

You can also play it in [xemu](https://xemu.app).

## Download

Grab the newest release zip (`OpenCrossing-Xbox-v1.zip` or later) from the [Releases](../../releases) page and unzip it. Inside:

```
OpenCrossing/
  default.xbe      the game
  default.tbn      dashboard icon
tools/
  make-xiso        packs the game and your disc image into a burnable ISO
  gc_trim_ciso.py  shrinks your disc image to the ~28 MB the game reads
  gcs_to_gci.py    converts a GameCube save export for use on the Xbox
```

Then pick one of the three ways to play below.

## Option 1: install to the hard drive

The easiest way, and the fastest to load.

1. Put your disc image in the `OpenCrossing` folder, next to `default.xbe`. The name doesn't matter.
2. Connect to the Xbox with an FTP client such as FileZilla. Your dashboard shows the Xbox's IP address. On most dashboards the login is `xbox` with the password `xbox`.
3. Copy the whole `OpenCrossing` folder to wherever your dashboard looks for games or apps, for example `E:\Games\` or `F:\Applications\`.
4. Start OpenCrossing from the dashboard. Look for the leaf icon.

The full 1.4 GB disc image works. If you'd rather save the space, shrink it first and copy the `.ciso` instead (about 28 MB):

```sh
python3 tools/gc_trim_ciso.py "Animal Crossing.iso" AnimalCrossing.ciso
```

Your folder should end up like this:

```
E:\Games\OpenCrossing\
  default.xbe
  default.tbn
  Animal Crossing.iso    (yours)
```

## Option 2: burn a disc

`make-xiso` packs `default.xbe` and your disc image into one Xbox ISO. It trims the image on the way, so the result is about 35 MB and fits on a CD-R.

1. You need `bash`, `python3` and [xdvdfs](https://github.com/antangelo/xdvdfs) on your PATH (grab a release binary or run `cargo install xdvdfs-cli`). On Windows, run the script from WSL or Git Bash.
2. Run:

   ```sh
   tools/make-xiso OpenCrossing/default.xbe "Animal Crossing.iso" OpenCrossing.iso
   ```

   Add `--full` before the XBE path if you want the untrimmed image instead (about 1.5 GB, needs a DVD-R).
3. Burn `OpenCrossing.iso` as a plain image at a low speed, with ImgBurn or similar. Don't let the burner convert it.
4. Put the disc in your modded Xbox and start it from the dashboard.

Not every Xbox drive reads burned discs. Samsung drives are usually the friendliest and Thomson the fussiest. If yours won't read it, use Option 1.

## Option 3: xemu

Build the ISO as in Option 2 and load it in xemu with Machine > Load Disc. Set xemu's memory to 64 MB. Sound works. Your saves go to xemu's virtual hard drive.

## Saves

Saves always go to the hard drive, even when you play from a disc, in `E:\UDATA\4f430001\save\card_a\`. They're regular GameCube `.gci` files, so you can take them back to Dolphin or the [PC port](https://github.com/flyngmt/ACGC-PC-Port).

To bring a town over from your GameCube or Dolphin:

1. Export the save. Dolphin's memory card manager gives you a `.gci`. GameShark and GC Memcard Manager exports are `.gcs`.
2. A `.gcs` needs converting: `python3 tools/gcs_to_gci.py mysave.gcs` writes `DobutsunomoriP_MURA.gci`. A `.gci` from Dolphin is fine as it is.
3. Start the game once so it creates its folders, then FTP the `.gci` to `E:\UDATA\4f430001\save\card_a\`. Any name ending in `.gci` works, including Dolphin's `01-GAFE-DobutsunomoriP_MURA.gci`.
4. If you've already started a town on the Xbox, move its `DobutsunomoriP_MURA.gci` out of that folder first. A file with exactly that name always loads before any other.

Every save keeps the previous three as `.bak1` to `.bak3`. Still, copy the folder off now and then.

**Visiting a friend's town:** put their town's `.gci` in `E:\UDATA\4f430001\save\card_b\` and take the train from your station. Without another town there, the Porter tells you there's no town data. Town visits are new on the Xbox, so back up both saves before you try one.

## Settings

Settings live in **Options** on the title screen, or **Settings** in the pause menu (the Back button). Changes take effect when you press Apply.

| tab | what's there |
|---|---|
| Video | Output (Auto picks the best mode your dashboard allows and shows which, like "Auto, 720p"; 480i or 480p keeps the game there; needs a restart, which the menu offers), Widescreen (Auto follows the dashboard, or pick 4:3 or 16:9; 720p is always 16:9), texture filter, FPS counter, screenshots |
| Audio | master volume |
| Controls | dead zones for both sticks, rumble strength, button remapping |
| Gameplay | Mr. Resetti, shop upgrade (Singleplayer lets Nook's shop grow to Nookington's without a visitor from another town), borderless acres (Off brings back the original acre-by-acre camera), NES aspect |

Everything is saved to `E:\UDATA\4f430001\settings.ini`, which you can also edit over FTP.

**Quit Game**, on the title screen or in the pause menu, takes you back to the dashboard.

**Screenshots:** turn them on in Options > Video, then click the right stick. Each one is saved as `shot00.bmp`, `shot01.bmp` and so on in `E:\UDATA\4f430001\`. Handy for bug reports.

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
| Right stick click | screenshot (when turned on) |

You can remap buttons in Options > Controls > Buttons.

Both dead zones start at 40%, which keeps an old controller with a loose stick from walking on its own. If yours is in good shape, try 15 to 20% in Options > Controls so small pushes count. The page shows where your stick is resting right now, so set the dead zone just above that.

## Troubleshooting

- **Black screen.** Some TVs can't show 720p or 480p even when the dashboard allows it. Hold Back on the controller while OpenCrossing starts, until the splash screen goes away. That start runs at 480i and remembers it. You can pick something else later in Options > Video, or turn 720p off in the dashboard.
- **Picture squashed or stretched.** Widescreen follows the dashboard's setting. Change it there, or pick 4:3 or 16:9 in Options > Video. 720p is always 16:9.
- **Updating from an older build.** The first start switches the old video defaults (480, 4:3) to Auto. If you'd chosen 480p or 4:3 yourself, choose it again. A 480i choice stays.
- **No sound.** Turn the console fully off and on again. After a crash, a reset or in-game reset (IGR) can leave the sound chip stuck, and only a power-off clears it.
- **Your character walks on its own, or menus scroll by themselves.** Raise the dead zone in Options > Controls.
- **A save doesn't load.** It has to be in `E:\UDATA\4f430001\save\card_a\` and end in `.gci`. A file named exactly `DobutsunomoriP_MURA.gci` always wins.
- **128 MB or upgraded consoles.** They work. The game uses 64 MB, like a stock Xbox.

## Known issues

- Clu Clu Land D is a Famicom Disk System game, which the NES emulator here can't run yet. Choosing it takes you back to the room.
- At 16:9 some 2D art, like the title logo, is drawn a little too wide.
- On the title screen, the menu text is drawn behind the player and the trees, so it can be hard to read.

The full list, with technical detail, is in [docs/known-issues.md](docs/known-issues.md).

## Reporting a bug

Open an issue and attach whichever of these exist in `E:\UDATA\4f430001\`: `crash.log`, `hang.log`, `last.log`, `perf.log` and `boot.log`. After a restart the previous run's logs are renamed `crash_prev.log`, `boot_prev.log` and so on, so grab those too. A screenshot or a photo of the screen helps, and so does knowing whether your Xbox has upgrades (RAM, CPU, modchip) and which cable you use.

## Building from source

You need Docker, Python 3 and Pillow.

```sh
xbox/build-image.sh   # once: builds the nxdk SDK image, about 10 minutes
xbox/build.sh         # writes build-xbox/xbe/default.xbe
```

[docs/toolchain.md](docs/toolchain.md) covers testing in xemu and on a real Xbox, and [docs/architecture.md](docs/architecture.md) explains how the port fits together. Work happens on the `dev` branch. Every push to `main` is published, as a numbered beta or, when it's tagged for one, a full release.

## Legal

This repository has no game assets, no ROM data and no Nintendo code. It holds the decompiled C source (CC0, ACreTeam) and the port code (MIT). You need your own legally obtained copy of Animal Crossing. Please don't open issues asking for ROMs.

Not affiliated with or endorsed by Nintendo or Microsoft. Animal Crossing is a trademark of Nintendo. Xbox is a trademark of Microsoft.

## Credits

- [ACreTeam/ac-decomp](https://github.com/ACreTeam/ac-decomp), the decompilation this is built on.
- [flyngmt/ACGC-PC-Port](https://github.com/flyngmt/ACGC-PC-Port) and its contributors, for the PC port this one grew out of (the GX to GL layer, the disc reader, frame-rate fixes, NES support).
- [XboxDev/nxdk](https://github.com/XboxDev/nxdk), [xemu](https://xemu.app) and [xdvdfs](https://github.com/antangelo/xdvdfs): the open Xbox toolchain, emulator and ISO packer.
- AI tools (Claude) were used in developing this port.

See [LICENSE](LICENSE).
