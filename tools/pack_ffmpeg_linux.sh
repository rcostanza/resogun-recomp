#!/usr/bin/env bash
# Rewrites prebuilt/ffmpeg-linux.tar.xz (tools/build_ffmpeg.sh --pack) in an Ubuntu 24.04 container,
# the system the Linux release is built on, so its libraries need no newer glibc than the release.
# Runs from Git Bash on Windows or from Linux; needs Docker.
set -euo pipefail
cd -- "$(dirname -- "$0")/.."
root=$(pwd -W 2>/dev/null || pwd)
MSYS_NO_PATHCONV=1 docker run --rm -v "$root:/w" -w /w -e OWNER="$(id -u):$(id -g)" ubuntu:24.04 bash -c '
    set -euo pipefail
    apt-get update -qq
    DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends \
        clang-18 nasm make zlib1g-dev curl ca-certificates xz-utils >/dev/null
    CC=clang-18 bash tools/build_ffmpeg.sh --pack
    chown -R "$OWNER" prebuilt out/downloads'
