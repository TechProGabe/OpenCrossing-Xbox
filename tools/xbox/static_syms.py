#!/usr/bin/env python3
"""Static (file-local) functions for a link map: writes <map>.statics.

    tools/xbox/static_syms.py [--map build-xbox/ac_xbox.map] [--nm llvm-nm]

lld-link's map lists only public symbols, so a profile sample in a static
function is credited to the public function before it. Every object has a
single .text section, so a static function sits at its offset from the
object's .text start, and that start follows from any public symbol of the
same object (map address minus its offset). Run it right after the build
whose map it is: it reads build-xbox's objects. prof_report.py and
console.py stage call it. (From Melee-X's tools/xbox/static_syms.py.)"""
import argparse
import pathlib
import re
import shutil
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[2]
LINE = re.compile(r'^\s*[0-9a-f]{4}:[0-9a-f]{8}\s+(\S+)\s+([0-9a-f]{16})\s+(?:f\s+)?(?:i\s+)?(\S+)\s*$')


def find_nm():
    for c in ('llvm-nm', '/opt/homebrew/opt/llvm/bin/llvm-nm', '/usr/local/opt/llvm/bin/llvm-nm'):
        p = shutil.which(c) or (c if pathlib.Path(c).exists() else None)
        if p:
            return p
    return None


def public_syms(map_path):
    out = {}
    for line in open(map_path, errors='replace'):
        m = LINE.match(line)
        if m:
            out.setdefault((m[1], m[3].split(':')[-1].lower()), int(m[2], 16))
    return out


def load_syms(map_path, statics=True):
    """(address, name, object) for the map's public symbols plus the static
    functions in <map>.statics, sorted; (re)builds the .statics file from
    build-xbox's objects when it is missing or older than build-xbox's map."""
    syms = []
    for line in open(map_path, errors='replace'):
        m = LINE.match(line)
        if m:
            syms.append((int(m[2], 16), m[1], m[3]))
    st = pathlib.Path(str(map_path) + '.statics')
    stale = not st.exists() or st.stat().st_mtime < pathlib.Path(map_path).stat().st_mtime
    if statics and stale and pathlib.Path(map_path).resolve().parent == (ROOT / 'build-xbox').resolve():
        build(map_path)   # build-xbox's map changes with every build
    if statics and st.exists():
        for line in open(st):
            p = line.split()
            if len(p) == 3:
                syms.append((int(p[0], 16), p[1], p[2]))
    syms.sort()
    return syms


def build(map_path, nm=None, build_dir=None):
    nm = nm or find_nm()
    if not nm:
        return None
    build_dir = pathlib.Path(build_dir or pathlib.Path(map_path).parent)
    public = public_syms(map_path)
    lines, missed = [], 0
    for obj in sorted(build_dir.rglob('*.obj')):
        res = subprocess.run([nm, '--defined-only', str(obj)], capture_output=True, text=True).stdout
        syms = [(int(m[1], 16), m[2], m[3]) for m in
                (re.match(r'^([0-9a-f]{8}) ([Tt]) (\S+)$', l) for l in res.splitlines()) if m]
        base = None
        for off, kind, name in syms:
            va = public.get((name, obj.name.lower()))
            if kind == 'T' and va is not None:
                base = va - off
                break
        if base is None:
            missed += 1
            continue
        lines += [f'{base + off:08x} {name} {obj.name}' for off, kind, name in syms if kind == 't']
    out = pathlib.Path(str(map_path) + '.statics')
    out.write_text('\n'.join(sorted(lines)) + '\n')
    return out, len(lines), missed


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--map', default=str(ROOT / 'build-xbox' / 'ac_xbox.map'))
    ap.add_argument('--nm')
    a = ap.parse_args()
    r = build(a.map, a.nm)
    if not r:
        raise SystemExit('llvm-nm not found (brew install llvm)')
    print(f'{r[0]}: {r[1]} static functions ({r[2]} objects without a public symbol to place them)')


if __name__ == '__main__':
    main()
