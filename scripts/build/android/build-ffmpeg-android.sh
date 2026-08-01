#!/bin/bash
# build-ffmpeg-android.sh
#
# Build a minimal STATIC FFmpeg for Android arm64-v8a, for audio decoding only.
#
# GeneralsX @build Claude 01/08/2026
#
# Codec scope is DERIVED from the shipped game assets, not guessed. Parsing the
# .big TOCs and running ffprobe over real entries gives exactly:
#
#   Audio.big    1720 files  .wav  -> pcm_s16le      s16   22050 Hz mono
#   Speech*.big  2430 files  .wav  -> adpcm_ima_wav  s16p  44100 Hz mono
#   Music.big      49 files  .mp3  -> mp3            fltp  44100 Hz stereo
#
# Hence: demuxers wav+mp3, decoders pcm_s16le+adpcm_ima_wav+mp3, parser mpegaudio.
#
# Deliberately NOT built:
#   swresample - nothing in the engine calls swr_*; all three formats map directly
#                onto getALFormat (mono16 / mono16 / stereo float32), and OpenAL
#                takes the sample rate per buffer, so no resampling is needed.
#   swscale    - video stays on BinkVideoPlayer; this tier is audio-only.
#   pcm_s24le  - no 24-bit assets exist.
#
# Output: build/ffmpeg-android/{include,lib} with exactly three archives
#         (libavformat.a, libavcodec.a, libavutil.a), consumed by
#         Core/GameEngineDevice/CMakeLists.txt via SAGE_ANDROID_FFMPEG_DIR.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
cd "${REPO_ROOT}"

FFMPEG_VERSION="${FFMPEG_VERSION:-7.1}"
FFMPEG_TARBALL="ffmpeg-${FFMPEG_VERSION}.tar.xz"
FFMPEG_URL="https://ffmpeg.org/releases/${FFMPEG_TARBALL}"
# Pinned from the official https://ffmpeg.org release of 7.1. Verified and fails
# closed on mismatch; override only if you also change FFMPEG_VERSION.
FFMPEG_SHA256="${FFMPEG_SHA256:-40973d44970dbc83ef302b0609f2e74982be2d85916dd2ee7472d30678a7abe6}"

WORK_DIR="${REPO_ROOT}/build/ffmpeg-src"
PREFIX="${REPO_ROOT}/build/ffmpeg-android"
API_LEVEL=24
ABI_ARCH=aarch64
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"

# ---------------------------------------------------------------- prerequisites

case "$(uname -s)" in
    Darwin) HOST_TAG="darwin-x86_64" ;;
    Linux)  HOST_TAG="linux-x86_64" ;;
    MINGW*|MSYS*|CYGWIN*) HOST_TAG="windows-x86_64" ;;
    *) echo "ERROR: unsupported host OS: $(uname -s)" >&2; exit 1 ;;
esac

if [ -z "${ANDROID_NDK_HOME:-}" ] || [ ! -d "${ANDROID_NDK_HOME}" ]; then
    echo "ERROR: ANDROID_NDK_HOME is not set or does not exist" >&2
    echo "  expected NDK r27, e.g. ~/Android/Sdk/ndk/27.1.12297006" >&2
    exit 1
fi

TOOLCHAIN="${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/${HOST_TAG}"
[ -d "${TOOLCHAIN}" ] || { echo "ERROR: toolchain not found: ${TOOLCHAIN}" >&2; exit 1; }

CC_BIN="${TOOLCHAIN}/bin/${ABI_ARCH}-linux-android${API_LEVEL}-clang"
[ -x "${CC_BIN}" ] || { echo "ERROR: compiler not found: ${CC_BIN}" >&2; exit 1; }

for tool in curl tar make sha256sum; do
    command -v "${tool}" >/dev/null 2>&1 || { echo "ERROR: missing required tool: ${tool}" >&2; exit 1; }
done

# ------------------------------------------------------- idempotent early-out

if [ -f "${PREFIX}/lib/libavformat.a" ] \
   && [ -f "${PREFIX}/lib/libavcodec.a" ] \
   && [ -f "${PREFIX}/lib/libavutil.a" ] \
   && [ -f "${PREFIX}/include/libavcodec/avcodec.h" ]; then
    echo "==> FFmpeg already built at ${PREFIX} (pass --force to rebuild)"
    if [ "${1:-}" != "--force" ]; then
        ls -la "${PREFIX}/lib/"*.a
        exit 0
    fi
    echo "==> --force given, rebuilding"
    rm -rf "${PREFIX}"
fi

# ------------------------------------------------------------------- fetch

mkdir -p "${WORK_DIR}"
cd "${WORK_DIR}"

if [ ! -f "${FFMPEG_TARBALL}" ]; then
    echo "==> Downloading ${FFMPEG_URL}"
    curl -fSL --retry 3 -o "${FFMPEG_TARBALL}.part" "${FFMPEG_URL}"
    mv "${FFMPEG_TARBALL}.part" "${FFMPEG_TARBALL}"
fi

COMPUTED_SHA="$(sha256sum "${FFMPEG_TARBALL}" | awk '{print $1}')"
echo "==> sha256(${FFMPEG_TARBALL}) = ${COMPUTED_SHA}"
if [ -n "${FFMPEG_SHA256}" ]; then
    if [ "${COMPUTED_SHA}" != "${FFMPEG_SHA256}" ]; then
        echo "ERROR: checksum mismatch" >&2
        echo "  expected ${FFMPEG_SHA256}" >&2
        echo "  got      ${COMPUTED_SHA}" >&2
        rm -f "${FFMPEG_TARBALL}"
        exit 1
    fi
    echo "==> checksum verified"
else
    echo "WARNING: FFMPEG_SHA256 is not pinned in this script."
    echo "         Pin the value above to fail closed on a corrupted/substituted tarball."
fi

SRC_DIR="${WORK_DIR}/ffmpeg-${FFMPEG_VERSION}"
if [ ! -d "${SRC_DIR}" ]; then
    echo "==> Extracting"
    tar xf "${FFMPEG_TARBALL}"
fi
cd "${SRC_DIR}"

# --------------------------------------------------------------- configure

echo "==> Configuring (audio-only, static, arm64-v8a, API ${API_LEVEL})"

# --disable-asm is a known fallback if the static+PIC NEON link fails; NEON is
# kept on by default because it materially improves mp3 decode.
EXTRA_CONFIGURE="${EXTRA_CONFIGURE:-}"

./configure \
    --prefix="${PREFIX}" \
    --target-os=android \
    --arch="${ABI_ARCH}" \
    --enable-cross-compile \
    --sysroot="${TOOLCHAIN}/sysroot" \
    --cc="${CC_BIN}" \
    --cxx="${TOOLCHAIN}/bin/${ABI_ARCH}-linux-android${API_LEVEL}-clang++" \
    --ar="${TOOLCHAIN}/bin/llvm-ar" \
    --nm="${TOOLCHAIN}/bin/llvm-nm" \
    --ranlib="${TOOLCHAIN}/bin/llvm-ranlib" \
    --strip="${TOOLCHAIN}/bin/llvm-strip" \
    --enable-static \
    --disable-shared \
    --enable-pic \
    --disable-autodetect \
    --disable-everything \
    --enable-avformat \
    --enable-avcodec \
    --enable-demuxer=wav,mp3 \
    --enable-decoder=pcm_s16le,adpcm_ima_wav,mp3 \
    --enable-parser=mpegaudio \
    --disable-swresample \
    --disable-swscale \
    --disable-avfilter \
    --disable-avdevice \
    --disable-postproc \
    --disable-programs \
    --disable-doc \
    --disable-network \
    --disable-protocols \
    --disable-zlib \
    --disable-lzma \
    --disable-bzlib \
    --disable-iconv \
    --disable-symver \
    --disable-debug \
    ${EXTRA_CONFIGURE}

# ------------------------------------------------------------------- build

echo "==> Building with ${JOBS} jobs"
make -j"${JOBS}"
make install

# ------------------------------------------------------------------ verify

echo ""
echo "==> Verifying output"
MISSING=0
for lib in libavformat.a libavcodec.a libavutil.a; do
    if [ -f "${PREFIX}/lib/${lib}" ]; then
        printf "    %-18s %s bytes\n" "${lib}" "$(stat -c%s "${PREFIX}/lib/${lib}" 2>/dev/null || stat -f%z "${PREFIX}/lib/${lib}")"
    else
        echo "    MISSING: ${lib}" >&2
        MISSING=1
    fi
done
for lib in libswresample.a libswscale.a; do
    if [ -f "${PREFIX}/lib/${lib}" ]; then
        echo "    WARNING: ${lib} was built but is not linked by this tier"
    fi
done
[ -f "${PREFIX}/include/libavcodec/avcodec.h" ] || { echo "    MISSING: include/libavcodec/avcodec.h" >&2; MISSING=1; }
[ -f "${PREFIX}/include/libavformat/avformat.h" ] || { echo "    MISSING: include/libavformat/avformat.h" >&2; MISSING=1; }

if [ "${MISSING}" -ne 0 ]; then
    echo "ERROR: build incomplete - refusing to leave a half-built prefix" >&2
    echo "       (CMake would half-detect it; removing ${PREFIX})" >&2
    rm -rf "${PREFIX}"
    exit 1
fi

echo ""
echo "Done. FFmpeg (audio-only, static) installed at:"
echo "  ${PREFIX}"
echo ""
echo "The android-vulkan preset points SAGE_ANDROID_FFMPEG_DIR here, so just rebuild:"
echo "  cmake --preset android-vulkan"
echo "  cmake --build build/android-vulkan --target z_generals"
