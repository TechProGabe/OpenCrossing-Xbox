#!/usr/bin/env python3
"""Fold the [PROF] lines of a log (xbox/src/xbox_prof.c, -DXBOX_PROF=1) into functions.

    tools/xbox/prof_report.py boot.log [boot2.log ...] [--map build-xbox/ac_xbox.map] [--last] [-n 40]

Each [PROF] entry is a 64-byte bucket address and a sample count. Buckets
are attributed to the function containing their start address (a bucket
straddling two functions goes to the first), and the functions are listed
by share of the samples placed in the image. --last uses only the final
report instead of summing all of them. [PROFL] (callers of memcpy/memset/
memcmp/memmove) and [PROFC] (every sample, one frame up) are folded into
the calling functions and listed after.

The map must come from the same build as the XBE that logged. Static
functions come from <map>.statics (tools/xbox/static_syms.py), built here
from build-xbox's objects when it is missing and the map is build-xbox's.
(From Melee-X's tools/xbox/prof_report.py.)"""
import argparse
import bisect
import collections
import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).parent))
import static_syms  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[2]


ap = argparse.ArgumentParser()
ap.add_argument('logs', nargs='+')
ap.add_argument('--map', default=str(ROOT / 'build-xbox' / 'ac_xbox.map'))
ap.add_argument('--last', action='store_true')
ap.add_argument('-n', type=int, default=40)
a = ap.parse_args()

reports, cur = [], None
for log in a.logs:
    for line in pathlib.Path(log).read_text(errors='replace').splitlines():
        m = re.search(r'\[PROF\] (\d+) samples: (\d+) in image, (\d+) outside, (\d+) while waiting', line)
        if m:
            cur = {'total': int(m[1]), 'placed': int(m[2]), 'waiting': int(m[4]), 'buckets': collections.Counter(),
                   'libc': collections.Counter(), 'callers': collections.Counter(), 'libc_n': 0, 'callers_n': 0}
            reports.append(cur)
            continue
        if cur is None:
            continue
        m = re.search(r'\[PROF([LC])\] (\d+) samples', line)
        if m:
            cur[{'L': 'libc_n', 'C': 'callers_n'}[m[1]]] += int(m[2])
            continue
        for tag, key in (('[PROFL]', 'libc'), ('[PROFC]', 'callers'), ('[PROF]', 'buckets')):
            if tag in line:
                for addr, n in re.findall(r'([0-9a-f]{8}):(\d+)', line):
                    cur[key][int(addr, 16)] += int(n)
                break
if not reports:
    sys.exit('no [PROF] reports in ' + ', '.join(a.logs))
use = reports[-1:] if a.last else reports
total = sum(r['total'] for r in use)
placed = sum(r['placed'] for r in use)
waiting = sum(r['waiting'] for r in use)
syms = static_syms.load_syms(a.map)
keys = [s[0] for s in syms]


def name_of(addr):
    i = bisect.bisect_right(keys, addr) - 1
    return f'{syms[i][1]} ({syms[i][2]})' if i >= 0 else '?'


funcs = collections.Counter()
for r in use:
    for addr, n in r['buckets'].items():
        funcs[name_of(addr)] += n
print(f'{len(use)} report(s): {total} samples ({waiting} more while the game thread waited), '
      f'{placed} in the image')
for name, n in funcs.most_common(a.n):
    print(f'{100.0 * n / max(placed, 1):5.1f}%  {n:6d}  {name}')


def callers(key, title):
    n_all = sum(r[key + '_n'] for r in use)
    if not n_all:
        return
    by = collections.Counter()
    for r in use:
        for addr, n in r[key].items():
            by[name_of(addr)] += n
    print(f'\n{title}: {n_all} samples ({100.0 * n_all / max(placed, 1):.1f}% of the image\'s)')
    for name, n in by.most_common(a.n):
        print(f'{100.0 * n / n_all:5.1f}%  {n:6d}  {name}')


callers('libc', 'memcpy/memset/memcmp/memmove, by calling function')
callers('callers', 'all samples, by calling function (one frame up)')
