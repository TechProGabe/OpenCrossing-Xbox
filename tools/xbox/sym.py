#!/usr/bin/env python3
"""Symbolize Xbox addresses against the lld-link map.
  tools/xbox/sym.py [build-xbox/ac_xbox.map] < addrs   (hex, one or more per line)
Prints: addr  symbol+off  object. Static functions come from <map>.statics
(static_syms.py), so a static isn't credited to the public function before it."""
import bisect, pathlib, re, sys

sys.path.insert(0, str(pathlib.Path(__file__).parent))
import static_syms  # noqa: E402

mp = sys.argv[1] if len(sys.argv) > 1 else "build-xbox/ac_xbox.map"
syms = static_syms.load_syms(mp)
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
