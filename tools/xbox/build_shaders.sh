#!/usr/bin/env bash
# Assemble xbox/shaders/*.vsh -> *_vsh.inl with nv2a-vsh (pip install nv2a-vsh).
# The .inl files are committed; rerun only after editing a .vsh.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
asm="${NV2AVSH:-nv2avsh}"
command -v "$asm" >/dev/null || asm="$HOME/xemu/vsh-venv/bin/nv2avsh"
for f in "$root"/xbox/shaders/*.vsh; do
  "$asm" "$f" "${f%.vsh}_vsh.inl"
  echo "$(basename "$f") -> $(grep -c '^0x' "${f%.vsh}_vsh.inl") instructions"
done
