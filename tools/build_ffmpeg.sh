#!/usr/bin/env bash
# Builds the minimal static FFmpeg the runner links (build.sh runs it when PREFIX is missing or was
# configured differently): only what Resogun uses. Its movies (video/title.mp4, menu.mp4) are MP4
# with H.264 video; AvPlayer also accepts AAC audio; save icons and the start-up picture are PNG.
# AvPlayer opens the movie's host path (the file protocol) or reads through the game's own I/O
# callbacks; no network protocol is built. The release then carries no FFmpeg DLLs or their codec
# libraries.
#
# A build of each platform is committed in prebuilt/ffmpeg-<os>.tar.xz with the version and options
# it was configured with; when they match this script it is unpacked instead of built
# (RESOGUN_FFMPEG_PREBUILT=0 builds from source anyway). --pack rebuilds from source and rewrites
# that archive: run it on Windows (MSYS2 CLANG64) and on Ubuntu 24.04 (the Linux release's glibc;
# tools/pack_ffmpeg_linux.sh does it in Docker) whenever the version or options change.
#
# usage: tools/build_ffmpeg.sh PREFIX     (CC names the compiler; nasm on PATH for the x86 assembly)
#        tools/build_ffmpeg.sh --pack
set -euo pipefail
pack=0
if [[ ${1:-} == --pack ]]; then pack=1; set -- out/ffmpeg-pack; fi
prefix=${1:?usage: tools/build_ffmpeg.sh PREFIX | --pack}
version=9.0.2
# The release tarball, checked against FFmpeg's release signing key (FCF986EA15E6E293A5644F10B4322F04D67658D8).
sha256=8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e
options=(
    --disable-everything --disable-autodetect --disable-programs --disable-doc --disable-network
    --disable-avdevice --disable-avfilter --disable-debug
    --enable-static --disable-shared --enable-zlib
    --enable-decoder=h264,aac,png --enable-parser=h264,aac,png --enable-demuxer=mov --enable-protocol=file
    --enable-swscale --enable-swresample
)
key="$version ${options[*]}"
stamp="$key ${CC:-clang}"
if ((!pack)) && [[ -f $prefix/.stamp && $(<"$prefix/.stamp") == "$stamp" ]]; then exit 0; fi
case $(uname -s) in MINGW*|MSYS*) os=windows ;; *) os=linux ;; esac
prebuilt=$(cd -- "$(dirname -- "$0")/.." && pwd)/prebuilt/ffmpeg-$os.tar.xz
# --pack from Git Bash: the MSYS2 CLANG64 clang and nasm first on PATH, as build.sh does.
msys2_root=${MSYS2_ROOT:-/c/msys64}
if ((pack)) && [[ $os == windows && -d $msys2_root/clang64/bin ]]; then
    export PATH="$msys2_root/clang64/bin:$msys2_root/usr/bin:$PATH"
fi

# The prebuilt archive and FFmpeg's source are both .tar.xz.
command -v xz >/dev/null || { echo 'FFmpeg: need xz (the xz-utils package).' >&2; exit 1; }

# fix_pc DIR: the pkg-config files name DIR, wherever the prefix was built or unpacked.
fix_pc() {
    local dir; dir=$(cd "$1" && pwd)
    command -v cygpath >/dev/null && dir=$(cygpath -m "$dir")
    sed -i -e "s|^prefix=.*|prefix=$dir|" -e 's|^libdir=.*|libdir=${prefix}/lib|' \
        -e 's|^includedir=.*|includedir=${prefix}/include|' "$1"/lib/pkgconfig/*.pc
}
if ((!pack)) && [[ ${RESOGUN_FFMPEG_PREBUILT:-1} != 0 && -f $prebuilt ]]; then
    if [[ $(tar -xJOf "$prebuilt" ./.key) == "$key" ]]; then
        rm -rf "$prefix" && mkdir -p "$prefix"
        # -m: unpacked now, so the next build relinks against them (the archive holds no times).
        tar -xJmf "$prebuilt" -C "$prefix" --exclude=./.key
        fix_pc "$prefix"
        echo "$stamp" > "$prefix/.stamp"
        echo "FFmpeg $version: unpacked prebuilt/ffmpeg-$os.tar.xz into $prefix"
        exit 0
    fi
    echo "FFmpeg: prebuilt/ffmpeg-$os.tar.xz was built with another version or options; building from source (--pack updates it)." >&2
fi

((pack)) && ! command -v nasm >/dev/null && { echo 'FFmpeg --pack needs nasm.' >&2; exit 1; }
command -v nasm >/dev/null || echo 'FFmpeg: no nasm, building without the x86 assembly (slower decoding).' >&2
asm=(); command -v nasm >/dev/null || asm=(--disable-x86asm)
mkdir -p out/downloads
archive=out/downloads/ffmpeg-$version.tar.xz
if [[ ! -f $archive ]]; then
    # ffmpeg.org drops connections now and then.
    curl -fsSL --retry 5 --retry-all-errors --retry-delay 10 -o "$archive.part" "https://ffmpeg.org/releases/ffmpeg-$version.tar.xz"
    mv "$archive.part" "$archive"
fi
if ! echo "$sha256  $archive" | sha256sum -c --status; then
    rm -f "$archive"
    echo "FFmpeg: $archive does not match its SHA-256; removed, run the build again." >&2; exit 1
fi
work=out/ffmpeg-build
rm -rf "$work" "$prefix"
mkdir -p "$work"
tar -xJf "$archive" -C "$work" --strip-components=1
prefix_abs=$(mkdir -p "$prefix" && cd "$prefix" && pwd)
# Windows: D:/... (the native pkgconf and clang read the .pc files, not MSYS's /d/...).
command -v cygpath >/dev/null && prefix_abs=$(cygpath -m "$prefix_abs")
echo "FFmpeg $version: configuring the minimal build (log: $work/build.log)"
(
    cd "$work"
    ./configure --prefix="$prefix_abs" --cc="${CC:-clang}" "${options[@]}" "${asm[@]}"
    make -j"$(nproc 2>/dev/null || echo 4)"
    make install
) > "$work/build.log" 2>&1 || { tail -30 "$work/build.log" >&2; echo "FFmpeg build failed (log: $work/build.log)" >&2; exit 1; }
rm -rf "$work"
if ((pack)); then
    # No times, owners or build folder in the archive's files list; the .pc files get theirs on unpacking.
    fix_pc "$prefix"
    sed -i 's|^prefix=.*|prefix=/ffmpeg|' "$prefix"/lib/pkgconfig/*.pc
    echo "$key" > "$prefix/.key"
    mkdir -p "$(dirname -- "$prebuilt")"
    (cd "$prefix" && tar --sort=name --owner=0 --group=0 --numeric-owner --mtime=@0 -cf - .) | xz -9e > "$prebuilt"
    rm -rf "$prefix"
    echo "FFmpeg $version: wrote $prebuilt ($(du -h "$prebuilt" | cut -f1))"
    exit 0
fi
echo "$stamp" > "$prefix/.stamp"
echo "FFmpeg $version: built into $prefix"
