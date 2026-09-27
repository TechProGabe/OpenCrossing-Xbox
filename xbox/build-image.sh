#!/usr/bin/env bash
# Build the opencrossing-xbox:sdk image (nxdk). Idempotent; slow on first run.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
DOCKER_BUILDKIT=0 docker build -t opencrossing-xbox:sdk "$here/docker"
