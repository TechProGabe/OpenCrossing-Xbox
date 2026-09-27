#!/usr/bin/env python3
"""Turn a GameCube save export into the .gci file the Xbox port reads.

  tools/gcs_to_gci.py <save.gcs | save.gci> [out_dir]

Accepts .gcs (GameShark / GC Memcard Manager: a 0x110-byte "GCSAVE" header
in front of a plain GCI) and .gci (Dolphin's export; checked and copied).
Writes <out_dir>/DobutsunomoriP_MURA.gci (the name comes from the save
itself), ready to go in E:\\UDATA\\4f430001\\save\\card_a\\ on the Xbox.
"""
import os
import struct
import sys

GCS_MAGIC = b"GCSAVE"
GCS_HEADER = 0x110


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    src = sys.argv[1]
    out_dir = sys.argv[2] if len(sys.argv) > 2 else "."
    data = open(src, "rb").read()

    if data.startswith(GCS_MAGIC):
        data = data[GCS_HEADER:]
    elif data.startswith(b"DATELGC_SAVE"):
        sys.exit("MaxDrive .sav is not supported: import it into Dolphin's memory "
                 "card manager and export a .gci instead")

    if len(data) < 0x40 or data[0:4] != b"GAFE":
        sys.exit(f"{src}: not an Animal Crossing (USA, GAFE) save")
    blocks = struct.unpack_from(">H", data, 0x38)[0]
    want = 0x40 + blocks * 0x2000
    if len(data) != want:
        sys.exit(f"{src}: size {len(data)} does not match its header ({want} bytes)")

    name = data[0x08:0x28].split(b"\0")[0].decode("ascii")
    os.makedirs(out_dir, exist_ok=True)
    out = os.path.join(out_dir, name + ".gci")
    with open(out, "wb") as f:
        f.write(data)
    print(f"wrote {out} ({blocks} blocks)")


if __name__ == "__main__":
    main()
