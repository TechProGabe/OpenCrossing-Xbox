#!/usr/bin/env python3
"""Symbolize Xbox addresses against the lld-link map.
  tools/xbox/sym.py [build-xbox/ac_xbox.map] < addrs   (hex, one or more per line)
Prints: addr  symbol+off  object"""
import bisect, re, sys

mp = sys.argv[1] if len(sys.argv) > 1 else "build-xbox/ac_xbox.map"
syms = []
for line in open(mp, errors="replace"):
    m = re.match(r"\s*[0-9a-f]{4}:[0-9a-f]{8}\s+(\S+)\s+([0-9a-f]{16})\s+(.*)", line)
    if m:
        syms.append((int(m.group(2), 16), m.group(1), m.group(3).strip()))
syms.sort()
keys = [s[0] for s in syms]
for line in sys.stdin:
    for tok in re.findall(r"(?:0x)?([0-9a-fA-F]{6,8})", line):
        a = int(tok, 16)
        i = bisect.bisect_right(keys, a) - 1
        if i < 0 or a >= 0x80000000:
            print(f"{a:08x}  ?")
            continue
        base, name, obj = syms[i]
        print(f"{a:08x}  {name}+0x{a - base:x}  {obj}")
