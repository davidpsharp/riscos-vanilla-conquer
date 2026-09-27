#!/usr/bin/env bash
# Run a command inside the RISC OS cross-build container with the repo mounted
# at /work. Builds the image on first use.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
IMAGE=vc-riscos-sdk
if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
    docker build --platform linux/amd64 -t "$IMAGE" "$ROOT/riscos/docker"
fi
exec docker run --rm --platform linux/amd64 -v "$ROOT":/work -w /work "$IMAGE" "$@"
