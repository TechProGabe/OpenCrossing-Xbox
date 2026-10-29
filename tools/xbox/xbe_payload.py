#!/usr/bin/env python3
"""Append the boot screen's painted layers (xbox/src/xbox_splash.c) to
default.xbe, so a copy of the game can't leave them behind. Same idea as
Prime-X's make_intro.py.

  python3 tools/xbox/xbe_payload.py <default.xbe>

Everything goes after the XBE's last byte, not into a section: the kernel
loads sections and sizes the image from the header, and neither sees what
follows, so it costs no RAM until the boot reads it. Each entry starts on
a 4 KB boundary. The file ends with a footer:
    n x { tag[4], offset u32, size u32 }, xbe_len u32, n u32, version u32 (1), "OCPK"
Running this again replaces the payload (xbe_len says where the XBE ends).

Entries:
  SPWE SPLE SPST SPBL  xbox/assets/splash_{welcome,letter,stamped,blank}.png
        (tools/xbox/make_splash.py), 256 colours: u16 w, u16 h, u32 0,
        256 x u32 0x00RRGGBB, w*h u8. Needs Pillow; without it the boot
        screen falls back to plain colours.
"""
import pathlib
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
SPLASH = [('SPWE', 'welcome'), ('SPLE', 'letter'), ('SPST', 'stamped'), ('SPBL', 'blank')]
MAGIC = b'OCPK'
VERSION = 1


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
        print(f'xbe_payload: {tag.decode()} {len(data) >> 10} KB')


if __name__ == '__main__':
    main()
