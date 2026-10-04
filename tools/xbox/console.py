#!/usr/bin/env python3
"""Deploy a build to the console and pull its logs over FTP (any OS).

    tools/xbox/console.py stage v12            # build-xbox -> hw/stage-v12 + hw/ac_xbox.v12.map(.statics)
    tools/xbox/console.py deploy v12           # pull the old logs, upload hw/stage-v12, verify
    tools/xbox/console.py pull v12             # logs + settings.ini + shotNN.bmp -> hw/logs-v12
    tools/xbox/console.py rollback             # put the XBE before the last deploy back
    tools/xbox/console.py ls                   # what's in the log folder

hw/ is ~/xemu/ochw unless OCX_HW is set. The console's FTP server (the
dashboard's) is at OCX_FTP_HOST, login xbox/xbox unless OCX_FTP_USER /
OCX_FTP_PASS say otherwise; OCX_APP is the game's folder (default
/F/Applications/OpenCrossing). Deploy puts default.xbe and default.tbn in
/F/Applications/OpenCrossing/ next to the disc image and keeps the XBE it
replaces there as default.xbe.prev (rollback swaps it back). Before it
uploads, it pulls the console's logs into hw/logs-before-vNN and deletes
them, so the next boot's logs are the new build's alone. Keep each deployed
build's map: sym.py and prof_report.py need the map of the build that wrote
the log. (From Melee-X's tools/xbox/console.py.)
"""
import ftplib
import io
import os
import re
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).parent))
import static_syms  # noqa: E402

HW = Path(os.environ.get("OCX_HW", Path.home() / "xemu" / "ochw"))
HOST = os.environ.get("OCX_FTP_HOST", "")
USER = os.environ.get("OCX_FTP_USER", "xbox")
PASS = os.environ.get("OCX_FTP_PASS", "xbox")
APP = os.environ.get("OCX_APP", "/F/Applications/OpenCrossing")   # the XBE's folder on the console
UDATA = "/E/UDATA/4f430001"
LOGS = re.compile(r"^((boot\d?|last|perf|crash|hang)(_prev)?\.log|input\.log|stick\d\.log|nes_shot\.bmp)$")
KEEP = ("settings.ini",)   # pulled, never deleted
SHOTS = re.compile(r"^shot\d\d\.bmp$")   # screenshots: pulled, never deleted
FILES = ("default.xbe", "default.tbn")


def ver(v):
    return v if v.startswith("v") else "v" + v


def connect():
    if not HOST:
        sys.exit("set OCX_FTP_HOST to the Xbox's IP address (the dashboard shows it)")
    try:
        f = ftplib.FTP(HOST, timeout=15)
    except OSError as e:
        sys.exit(f"console not reachable at {HOST} ({e}): is the Xbox on, with the dashboard up?")
    f.login(USER, PASS)
    return f


def names(f, d):
    # the console's FTP server lists the current directory whatever path NLST is given
    f.cwd(d)
    return sorted(p.rsplit("/", 1)[-1] for p in f.nlst())


def fetch(f, names_, out):
    out.mkdir(parents=True, exist_ok=True)
    for n in names_:
        with open(out / n, "wb") as fh:
            f.retrbinary(f"RETR {UDATA}/{n}", fh.write)
        print(f"got {n} ({(out / n).stat().st_size} bytes)")


def stage(v):
    v = ver(v)
    out = HW / f"stage-{v}"
    out.mkdir(parents=True, exist_ok=True)
    for name in FILES:
        shutil.copy2(ROOT / "build-xbox" / "xbe" / name, out / name)
    m = HW / f"ac_xbox.{v}.map"
    shutil.copy2(ROOT / "build-xbox" / "ac_xbox.map", m)
    r = static_syms.build(str(m), build_dir=ROOT / "build-xbox")
    print(f"staged {out} and {m.name}" + (f" ({r[1]} static functions)" if r else " (no llvm-nm: no statics)"))


def deploy(v):
    v = ver(v)
    src = HW / f"stage-{v}"
    missing = [n for n in FILES if not (src / n).is_file()]
    if missing:   # before touching the console: its logs and rollback XBE stay as they are
        sys.exit(f"{src} has no {', '.join(missing)}: run `console.py stage {v}` first")
    f = connect()
    have = names(f, UDATA)
    old = [n for n in have if LOGS.match(n)]
    fetch(f, old + [n for n in have if n in KEEP or SHOTS.match(n)], HW / f"logs-before-{v}")
    for n in old:
        f.delete(f"{UDATA}/{n}")
    print(f"pulled and deleted {len(old)} old logs")
    if "default.xbe" in names(f, APP):
        cur = io.BytesIO()
        f.retrbinary(f"RETR {APP}/default.xbe", cur.write)
        f.storbinary(f"STOR {APP}/default.xbe.prev", io.BytesIO(cur.getvalue()))
        print(f"kept the previous XBE as default.xbe.prev ({len(cur.getvalue())} bytes)")
    for name in FILES:
        data = (src / name).read_bytes()
        f.storbinary(f"STOR {APP}/{name}", io.BytesIO(data))
        back = io.BytesIO()
        f.retrbinary(f"RETR {APP}/{name}", back.write)
        if back.getvalue() != data:
            sys.exit(f"{name} MISMATCH after upload")
        print(f"{name} ok ({len(data)} bytes)")
    f.quit()


def rollback():
    f = connect()
    if "default.xbe.prev" not in names(f, APP):
        sys.exit("no default.xbe.prev on the console")
    prev, cur = io.BytesIO(), io.BytesIO()
    f.retrbinary(f"RETR {APP}/default.xbe.prev", prev.write)
    f.retrbinary(f"RETR {APP}/default.xbe", cur.write)
    f.storbinary(f"STOR {APP}/default.xbe", io.BytesIO(prev.getvalue()))
    f.storbinary(f"STOR {APP}/default.xbe.prev", io.BytesIO(cur.getvalue()))
    print("swapped default.xbe and default.xbe.prev")
    f.quit()


def pull(v):
    f = connect()
    have = names(f, UDATA)
    fetch(f, [n for n in have if LOGS.match(n) or n in KEEP or SHOTS.match(n)], HW / f"logs-{ver(v)}")
    f.quit()


def main():
    cmds = {"stage": stage, "deploy": deploy, "pull": pull}
    if len(sys.argv) < 2 or sys.argv[1] not in (*cmds, "ls", "rollback"):
        sys.exit(__doc__)
    if sys.argv[1] == "ls":
        f = connect()
        print("\n".join(n for n in names(f, UDATA) if LOGS.match(n)) or "(no logs)")
        f.quit()
        return
    if sys.argv[1] == "rollback":
        rollback()
        return
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    cmds[sys.argv[1]](sys.argv[2])


if __name__ == "__main__":
    main()
