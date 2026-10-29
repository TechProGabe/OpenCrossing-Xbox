#!/usr/bin/env python3
"""Render the boot screen's painted layer: tools/xbox/splash/letter.html ->
xbox/assets/splash_{welcome,letter,stamped,blank}.png (640x480), with a
headless Edge or Chrome. Run it by hand after changing the page or the
logo, and commit the PNGs; the build only reads them
(tools/xbox/xbe_payload.py palettizes them into default.xbe).

  python tools/xbox/make_splash.py           # BROWSER=<path> to pick one

Needs the network once for the Fredoka font (Google Fonts).
"""
import os
import pathlib
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
PAGE = ROOT / 'tools/xbox/splash/letter.html'
OUT = ROOT / 'xbox/assets'
VARIANTS = ('welcome', 'letter', 'stamped', 'blank')


def find_browser():
    for c in (os.environ.get('BROWSER'),
              'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',
              'C:/Program Files/Google/Chrome/Application/chrome.exe',
              shutil.which('chromium'), shutil.which('google-chrome'), shutil.which('msedge'),
              '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'):
        if c and pathlib.Path(c).exists():
            return c
    sys.exit('make_splash: no Edge or Chrome found (set BROWSER=<path>)')


def main():
    browser = find_browser()
    for v in VARIANTS:
        out = OUT / f'splash_{v}.png'
        subprocess.run([browser, '--headless=new', '--disable-gpu', '--hide-scrollbars',
                        '--force-device-scale-factor=1', '--window-size=640,480', '--virtual-time-budget=5000',
                        f'--screenshot={out}', PAGE.as_uri() + '#' + v],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        print(f'make_splash: {out.relative_to(ROOT)}')


if __name__ == '__main__':
    main()
