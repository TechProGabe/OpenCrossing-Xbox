#!/usr/bin/env python3
"""Append the boot screen's data to default.xbe: the intro music played
while the first boot's disc check runs (xbox/src/xbox_intro.c) and the
painted layers of the boot screen (xbox/src/xbox_splash.c). Carried inside
the XBE so a copy of the game can't leave them behind. Same idea as
Prime-X's make_intro.py.

  python3 tools/xbox/xbe_payload.py <default.xbe>

Everything goes after the XBE's last byte, not into a section: the kernel
loads sections and sizes the image from the header, and neither sees what
follows, so it costs no RAM until the boot reads it. Each entry starts on
a 4 KB boundary. The file ends with a footer:
    n x { tag[4], offset u32, size u32 }, xbe_len u32, n u32, version u32 (1), "OCPK"
Running this again replaces the payload (xbe_len says where the XBE ends).

Entries:
  INTR  xbox/assets/intro.mp3 as IMA ADPCM, QuickTime packets
        (adpcm_ima_qt): 32 kHz stereo (the rate of the game's own ring in
        xbox_audio.c), volume baked in at 75%, 68 bytes for 64 frames, ~32
        KB a second. ffmpeg reads only the audio stream and writes raw
        packets (-f data): no tag, title or encoder string reaches the XBE.
        Cached as intro.qt beside the build folder. Needs ffmpeg (PATH, or
        FFMPEG=<path>); without it the check runs silent.
  SPWE SPLE SPST SPBL  xbox/assets/splash_{welcome,letter,stamped,blank}.png
        (tools/xbox/make_splash.py), 256 colours: u16 w, u16 h, u32 0,
        256 x u32 0x00RRGGBB, w*h u8. Needs Pillow; without it the boot
        screen falls back to plain colours.
"""
import os
import pathlib
import shutil
import struct
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
INTRO = ROOT / 'xbox/assets/intro.mp3'
SPLASH = [('SPWE', 'welcome'), ('SPLE', 'letter'), ('SPST', 'stamped'), ('SPBL', 'blank')]
VOLUME = 0.75
RATE = 32000
MAGIC = b'OCPK'
VERSION = 1


def find_ffmpeg():
    for c in (os.environ.get('FFMPEG'), shutil.which('ffmpeg'), 'C:/ProgramData/chocolatey/bin/ffmpeg.exe',
              '/c/ProgramData/chocolatey/bin/ffmpeg.exe', '/usr/bin/ffmpeg'):
        if c and pathlib.Path(c).exists():
            return c
    return None


def intro(cache):
    if not INTRO.exists():
        return None
    newest = max(INTRO.stat().st_mtime, pathlib.Path(__file__).stat().st_mtime)
    if cache.exists() and cache.stat().st_mtime >= newest:
        return cache.read_bytes()
    ffmpeg = find_ffmpeg()
    if not ffmpeg:
        print('xbe_payload: no ffmpeg, no music (the disc check runs silent)')
        return None
    tmp = cache.with_suffix('.tmp')
    subprocess.run([ffmpeg, '-hide_banner', '-loglevel', 'error', '-y', '-i', str(INTRO), '-map', '0:a:0',
                    '-map_metadata', '-1', '-af', f'volume={VOLUME}', '-ar', str(RATE), '-ac', '2',
                    '-c:a', 'adpcm_ima_qt', '-trellis', '8', '-f', 'data', str(tmp)], check=True)
    data = tmp.read_bytes()
    if not data or len(data) % 68:
        sys.exit(f'xbe_payload: {len(data)} bytes is not whole 68-byte stereo packets')
    tmp.replace(cache)
    return data


def splash(name):
    path = ROOT / f'xbox/assets/splash_{name}.png'
    try:
        from PIL import Image
    except ImportError:
        print('xbe_payload: no Pillow, no painted boot screen')
        return None
    if not path.exists():
        return None
    im = Image.open(path).convert('RGB')
    q = im.quantize(256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    pal = q.getpalette()[:768]
    pal += [0] * (768 - len(pal))
    w, h = q.size
    out = bytearray(struct.pack('<HHI', w, h, 0))
    for i in range(256):
        out += struct.pack('<I', pal[3 * i] << 16 | pal[3 * i + 1] << 8 | pal[3 * i + 2])
    out += q.tobytes()
    return bytes(out)


def strip(xbe):
    """the XBE without a payload this script appended before"""
    if len(xbe) >= 16 and xbe[-4:] == MAGIC:
        xlen, n, _ = struct.unpack_from('<III', xbe, len(xbe) - 16)
        if 16 + 12 * n <= len(xbe) and xlen <= len(xbe):
            return xbe[:xlen]
    return xbe


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    xbe_path = pathlib.Path(sys.argv[1])
    entries = []
    music = None if os.environ.get('XBOX_NO_INTRO') else intro(xbe_path.parent.parent / 'intro.qt')
    if music:
        entries.append((b'INTR', music))
    for tag, name in SPLASH:
        d = splash(name)
        if d:
            entries.append((tag.encode(), d))
    xbe = strip(xbe_path.read_bytes())
    if not entries:
        xbe_path.write_bytes(xbe)
        return
    out = bytearray(xbe)
    table = b''
    for tag, data in entries:
        out += bytes(-len(out) & 0xFFF)   # sector-aligned reads on the console
        table += tag + struct.pack('<II', len(out), len(data))
        out += data
    out += table + struct.pack('<III', len(xbe), len(entries), VERSION) + MAGIC
    xbe_path.write_bytes(out)
    for tag, data in entries:
        extra = f', {len(data) // 68 * 64 // RATE} s' if tag == b'INTR' else ''
        print(f'xbe_payload: {tag.decode()} {len(data) >> 10} KB{extra}')


if __name__ == '__main__':
    main()
