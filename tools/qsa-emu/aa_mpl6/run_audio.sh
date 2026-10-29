#!/bin/sh
# Boot MU0918 deva-ctrl-qc.so under the SDP io-audio in QEMU and drive
# mpl6_gal_ent like GAL: a SAD byte-layout probe, a stock client, the Android
# Auto upmix interposer preloaded with mode off, then with it on.  The ENT
# route is switched to Input=6 first, as the audio manager does when AA plays.
#
#   run_audio.sh stock|mirror [deva-ctrl-qc.so]
#
# Needs: qemu-system-arm, the qnx65-armv7-toolchain:8.5 Docker image, and the
# collected MU0918 driver (default .tmp/mu0918-audio next to the repo).
# "mirror" is that driver run through
# android_auto/deploy/patch_deva_mpl6_mirror_mpl1_mu0918.py.
# Harness-only differences from the car: SDP io-audio and libasound instead of
# the firmware's, luka's csd_stub instead of the DSP, mmap transport disabled.
set -eu
VARIANT=${1:?stock|mirror}
HERE=$(cd "$(dirname "$0")" && pwd)
Q=$HERE
REPO=$(cd "$HERE/../../.." && pwd)
TQ=${QNX_QEMU_DIR:-$REPO/../tools/qnx65-armv7-toolchain/tests/qemu}
DEVA_DIR=${MU0918_AUDIO_DIR:-$REPO/../.tmp/mu0918-audio}
case "$VARIANT" in
    stock) DEVA=${2:-$DEVA_DIR/deva-ctrl-qc.so.stock} ;;
    mirror) DEVA=${2:-$DEVA_DIR/deva-ctrl-qc.so.mpl6-mirror} ;;
    *) echo "bad variant" >&2; exit 2 ;;
esac
[ -f "$DEVA" ] || { echo "missing driver: $DEVA" >&2; exit 1; }
W=$REPO/build/qsa-emu/aa-mpl6-$VARIANT
rm -rf "$W"; mkdir -p "$W/fat"
echo ">> work dir $W"
cp "$TQ/startup-virt" "$TQ/libstartup.a" "$TQ/devc-serdebug" "$W/"
cp "$DEVA" "$W/deva-ctrl-qc.so"
cp "$Q/gal_probe.c" "$Q/extra_stubs.c" "$Q/probe_log.c" "$Q/ringstat.c" "$Q/route.c" "$W/"

cat > "$W/audio.build" <<'EOF'
[linker="(40;1;)qcc -bootstrap -nostdlib -Wl,--no-keep-memory -Vgcc_ntoarmv7%(m!=3,%(m!=6,%(e==0, -EL%)%(e==1, -EB%)%)%)%(h!=0, -Wl,-Ttext -Wl,0x%t%)%(d!=0, -Wl,-Tdata -Wl,0x%d%) -o%o %i %[M -L%^i -Wl,-uinit_%n -lmod_%n%] /w/libstartup.a -L/opt/qnx650/target/qnx6/armle-v7/lib -L/opt/qnx650/target/qnx6/armle-v7/usr/lib -llzo -lucl -ldrvr"]
[linker="(40;2;)cp %i %o"]
[image=0x40200000]
[virtual=armle-v7,raw] .bootstrap = {
    startup-virt -v -S
    PATH=:/proc/boot LD_LIBRARY_PATH=:/proc/boot procnto-smp -vvv
}
[+script] .script = {
    procmgr_symlink ../../proc/boot/libc.so.3 /usr/lib/ldqnx.so.2
    devc-serdebug -e -F -S
    waitfor /dev/ser1 4
    reopen /dev/ser1
    slogger -s 4096
    display_msg "@@AUDIO-START@@"
    devstub /dev/audio_service
    waitfor /dev/audio_service 5
    LD_PRELOAD=/proc/boot/libcsdIpcClient.so CACHE_NOP=1 io-audio -vvvvv -d qc platform_id=mib2,log_level=high,default_device=six_channel_speakerp,data_dump_path=/var/log/
    waitfor /dev/snd/pcmC0D4p 240
    waitfor /dev/snd/pcmPreferredp 240
    route
    route MS_ENT 0=6
    GALPROBE_SAD_PATTERN=1 gal_probe mpl6_gal_ent /tmp/runS.txt
    gal_probe mpl6_gal_ent /tmp/runA.txt
    ls -l /proc/boot/libaaupmix.so /proc/boot/gal_probe
    LD_PRELOAD=/proc/boot/libaaupmix.so gal_probe mpl6_gal_ent /tmp/runP.txt
    SQ5_AA_UPMIX=on LD_PRELOAD=/proc/boot/libaaupmix.so gal_probe mpl6_gal_ent /tmp/runB.txt
    display_msg "@@RUN-A stock GAL client@@"
    cat /tmp/runA.txt
    display_msg "@@RUN-P preload, mode off@@"
    cat /tmp/runP.txt
    display_msg "@@RUN-B GAL client + aa upmix hook@@"
    cat /tmp/runB.txt
    display_msg "@@RINGSTAT@@"
    ringstat
    display_msg "@@SLOG@@"
    sloginfo
    display_msg "@@AUDIO-END@@"
}
[type=link] /usr/lib/ldqnx.so.2=/proc/boot/libc.so.3
[type=link] /dev/console=/dev/ser1
[type=link] /tmp=/dev/shmem
[type=link] /var/log=/dev/shmem
[perms=+r,+x data=copy]
libc.so.3
libm.so.2
procnto-smp
devc-serdebug
slogger
sloginfo
waitfor
ls
io-audio
libasound.so.2
deva-ctrl-qc.so
libcsdIpcClient.so
libaaupmix.so
devstub
gal_probe
ringstat
cat
route
EOF

docker run --rm --platform=linux/amd64 -v "$W":/w -v "$REPO":/src:ro -w /w \
    qnx65-armv7-toolchain:8.5 sh -c '
set -e
export QNX_HOST=/opt/qnx650/host/linux/x86 QNX_TARGET=/opt/qnx650/target/qnx6
export PATH=$QNX_HOST/usr/bin:$PATH
T=$QNX_TARGET/armle-v7
CC=arm-unknown-nto-qnx6.5.0eabi-gcc
cp $T/lib/libc.so.3 $T/lib/libm.so.2 $T/boot/sys/procnto-smp $T/usr/sbin/slogger \
   $T/bin/sloginfo $T/bin/waitfor $T/bin/ls $T/sbin/io-audio $T/lib/libasound.so.2 /w/
# The serial base64 render dump takes minutes per ring; ringstat reads the raw
# /tmp ring files instead, so drop the dump from this copy of the stub.
sed "s/^            dma_emit_renders(index);/            (void)0;/" \
    /src/tools/qsa-emu/csd_stub.c > csd_stub.c
grep -q "(void)0;" csd_stub.c
$CC -shared -fPIC -O2 -Wl,-soname,libcsdIpcClient.so -o libcsdIpcClient.so \
    csd_stub.c extra_stubs.c
$CC -O2 -o devstub /src/tools/qsa-emu/devstub.c
$CC -O2 -Wall -o gal_probe gal_probe.c -lasound -lm
$CC -O2 -Wall -o ringstat ringstat.c -lm
$CC -O2 -Wall -o route route.c -lasound
cp $T/bin/cat /w/
$CC -shared -fPIC -O2 -march=armv7-a -mfloat-abi=softfp -mfpu=neon -std=gnu99 \
    -I/src/android_auto/hook/src -I/src/hook -I/src/toolchain/qnx65-abi/include \
    -o libaaupmix.so /src/android_auto/hook/src/audio_upmix.c probe_log.c \
    /src/hook/audio/stereo_upmix.c /src/hook/audio/scene_engine_math.c \
    /src/hook/audio/scene_clusters.c /src/hook/audio/scene_engine.c \
    /src/hook/audio/scene_mask.c /src/hook/audio/scene_mask_worker.c -lm
export MKIFS_PATH=/w:$T/boot/sys:$T/bin:$T/lib:$T/usr/lib
mkifs audio.build fat/ifs.bin
ls -l fat/ifs.bin' > "$W/build.log" 2>&1 || { tail -30 "$W/build.log"; exit 1; }
tail -2 "$W/build.log"

LOG=$W/qemu.log
( printf '\n'; sleep 4
  printf 'virtio scan\n'; sleep 2
  printf 'fatload virtio 0:1 0x40200000 ifs.bin\n'; sleep 3
  printf 'go 0x40200000\n'; sleep "${RUN_SECS:-420}" ) | \
  timeout "${RUN_TIMEOUT:-480}" qemu-system-arm -M virt -m 512 -cpu cortex-a15 \
    -bios "$TQ/u-boot.bin" -drive file=fat:rw:"$W/fat",format=raw,media=disk \
    -display none -serial stdio > "$LOG" 2>&1 || true
grep -c "" "$LOG"
grep -q "@@AUDIO-END@@" "$LOG" && echo "reached end" || echo "did NOT reach end"
