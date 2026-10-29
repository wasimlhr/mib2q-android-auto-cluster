#!/usr/bin/env bash
# Rebuild only the Android Auto LD_PRELOAD hook after a hook-source change.
# Reuses the already generated live_hook.c and does not rebuild/download FFmpeg.
set -euo pipefail

IMAGE=${QNX_IMAGE:-qnx65-armv7-toolchain:8.5}
ROOT=$(cd "$(dirname "$0")/.." && pwd)

test -f "$ROOT/build/android_auto/live_hook.c" || {
    echo "ERROR: build/android_auto/live_hook.c is missing; run build_android_auto_native.sh once." >&2
    exit 1
}

docker run --rm --platform=linux/amd64 \
    -v "$ROOT":/src \
    "$IMAGE" bash -lc '
        set -e
        export PATH=/opt/qnx650/host/linux/x86/usr/bin:$PATH
        export QNX_HOST=/opt/qnx650/host/linux/x86
        export QNX_TARGET=/opt/qnx650/target/qnx6
        B=$(mktemp -d /tmp/sq5-hook.XXXXXX)
        trap '\''rm -rf "$B"'\'' EXIT HUP INT TERM
        cp /src/android_auto/hook/vendor/*.[ch] /src/android_auto/hook/src/*.[ch] "$B/"
        cp /src/build/android_auto/live_hook.c "$B/live_hook.c"
        cd "$B"
        arm-unknown-nto-qnx6.5.0eabi-gcc \
            -O2 -ftree-vectorize -march=armv7-a -mfloat-abi=softfp -mfpu=neon \
            -std=gnu99 -Wall -Wextra -Werror -fPIC -shared \
            -I/src/hook -I/src/toolchain/qnx65-abi/include \
            live_hook.c transport.c unexpected.c uiconfig.c navxlate.c rotary.c \
            versionfix.c audio_upmix.c \
            /src/hook/audio/stereo_upmix.c \
            /src/hook/audio/scene_engine_math.c \
            /src/hook/audio/scene_clusters.c \
            /src/hook/audio/scene_engine.c \
            /src/hook/audio/scene_mask.c \
            /src/hook/audio/scene_mask_worker.c \
            -o libsq5_cluster_live.so -lc -lm
        arm-unknown-nto-qnx6.5.0eabi-readelf -Ws \
            libsq5_cluster_live.so |
            grep -E "snd_pcm_plugin_(prepare|flush)|snd_pcm_(plugin_)?write"
        cp libsq5_cluster_live.so /src/build/android_auto/libsq5_cluster_live.so
    '

sha256sum "$ROOT/build/android_auto/libsq5_cluster_live.so"
