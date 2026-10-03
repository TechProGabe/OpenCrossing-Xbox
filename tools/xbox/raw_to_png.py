#!/usr/bin/env python3
"""nes_shot.raw (xbox_nv2a.c, -DXBOX_NES_SHOT) -> PNG.

    tools/xbox/raw_to_png.py nes_shot.raw out.png
"""
import struct
import sys

from PIL import Image


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    d = open(sys.argv[1], "rb").read()
    magic, w, h, bpp = struct.unpack("<4I", d[:16])
    if magic != 0x5358434F:
        sys.exit("not an OCXS shot")
    px = d[16:]
    im = Image.new("RGB", (w, h))
    out = im.load()
    for y in range(h):
        for x in range(w):
            if bpp == 16:
                v = px[(y * w + x) * 2] | (px[(y * w + x) * 2 + 1] << 8)
                r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
                out[x, y] = ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2))
            else:
                b, g, r = px[(y * w + x) * 4:(y * w + x) * 4 + 3]
                out[x, y] = (r, g, b)
    im.save(sys.argv[2])
    print(f"{sys.argv[2]}: {w}x{h} from {bpp}-bit")


if __name__ == "__main__":
    main()
