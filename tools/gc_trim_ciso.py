#!/usr/bin/env python3
"""Trim a GameCube disc image to a CISO holding only the data the game uses.

  tools/gc_trim_ciso.py <in.iso|.gcm> <out.ciso>

A GameCube disc is 1.4 GB, but Animal Crossing's files are ~27 MB; the rest
is padding. This keeps the system area (header, apploader, DOL, FST: every
byte up to the end of the FST), the DOL, and every file the FST lists, and
drops every other block. The output is the CISO layout pc/src/pc_disc.c
reads: a 0x8000-byte header ("CISO", LE u32 block size, one present-byte per
block), then the present blocks in order. Absent blocks read as zeros.
Small enough for a CD-R, and faster to burn and copy.
"""
import struct
import sys

BLOCK = 0x10000          # 64 KB: 1.4 GB / 64 KB fits the 0x7FF8-entry map
HDR = 0x8000
MAP_MAX = HDR - 8


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    src, dst = sys.argv[1], sys.argv[2]
    with open(src, "rb") as f:
        f.seek(0, 2)
        size = f.seek(0, 2)
        f.seek(0)
        head = f.read(0x440)
        if head[:4] == b"CISO":
            sys.exit("already a CISO")
        game_id = head[:6].decode("ascii", "replace")
        dol_off, fst_off, fst_size = struct.unpack(">III", head[0x420:0x42C])

        keep = [(0, fst_off + fst_size)]
        f.seek(dol_off)
        dh = f.read(0x100)
        text_off = struct.unpack(">7I", dh[0x00:0x1C])
        data_off = struct.unpack(">11I", dh[0x1C:0x48])
        text_sz = struct.unpack(">7I", dh[0x90:0xAC])
        data_sz = struct.unpack(">11I", dh[0xAC:0xD8])
        dol_end = max([o + s for o, s in zip(text_off + data_off, text_sz + data_sz) if s] + [0x100])
        keep.append((dol_off, dol_off + dol_end))

        f.seek(fst_off)
        fst = f.read(fst_size)
        n_ent = struct.unpack(">I", fst[8:12])[0]
        files = 0
        payload = 0
        for i in range(1, n_ent):
            flags_name, off, length = struct.unpack(">III", fst[i * 12:i * 12 + 12])
            if flags_name >> 24:        # directory
                continue
            keep.append((off, off + length))
            files += 1
            payload += length

        nblocks = (size + BLOCK - 1) // BLOCK
        if nblocks > MAP_MAX:
            sys.exit(f"image too large for CISO map ({nblocks} blocks)")
        present = bytearray(nblocks)
        for a, b in keep:
            for blk in range(a // BLOCK, (max(a, b - 1)) // BLOCK + 1):
                if blk < nblocks:
                    present[blk] = 1

        with open(dst, "wb") as o:
            o.write(b"CISO" + struct.pack("<I", BLOCK) + bytes(present) + bytes(MAP_MAX - nblocks))
            for blk in range(nblocks):
                if present[blk]:
                    f.seek(blk * BLOCK)
                    data = f.read(BLOCK)
                    o.write(data + bytes(BLOCK - len(data)))
    kept = sum(present)
    print(f"{game_id}: {files} files ({payload / 1e6:.1f} MB), kept {kept}/{nblocks} blocks "
          f"-> {dst} ({(HDR + kept * BLOCK) / 1e6:.1f} MB)")


if __name__ == "__main__":
    main()
