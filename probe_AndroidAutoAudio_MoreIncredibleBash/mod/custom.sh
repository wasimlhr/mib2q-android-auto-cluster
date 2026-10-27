#!/bin/sh
# Reversible Android Auto media-format negotiation probe.
#
# This package is deliberately separate from the cockpit installer. It changes
# only audio.sink.media.bits in gal.json and never installs a hook or binary.
set -u
PATH=/proc/boot:/bin:/usr/bin:/usr/sbin:/sbin:/mnt/app/armle/bin:/mnt/app/armle/usr/bin
export PATH
unset LD_PRELOAD

case $0 in */*) MOD=${0%/*} ;; *) MOD=. ;; esac
MOD=$(cd "$MOD" && pwd) || exit 1
TEST_ROOT=${MIB2Q_AA_TEST_ROOT:-}

if [ -z "$TEST_ROOT" ] && [ ! -d /mnt/system/etc/eso/production ] && \
   [ -d /net/mmx/mnt/system/etc/eso/production ]; then
    exec on -f mmx /bin/sh "$MOD/custom.sh" "$@"
fi

rooted() { if [ -n "$TEST_ROOT" ]; then echo "$TEST_ROOT$1"; else echo "$1"; fi; }
SD=$(rooted /fs/sda0)
[ -d "$SD" ] || SD=$(rooted /net/mmx/fs/sda0)
GJ=$(rooted /mnt/system/etc/eso/production/gal.json)
SYS=$(rooted /mnt/system)
BAK=$GJ.sq5audio24bak
ON=$SD/sq5_aa_audio_24_on
ONCE=$SD/sq5_aa_audio_24_once
RESTORE=$SD/sq5_aa_audio_restore

size_of() { set -- $(ls -l "$1" 2>/dev/null); echo ${5:-0}; }
sum_of() { set -- $(cksum "$1" 2>/dev/null); echo "${1:-0} ${2:-0}"; }
remount_rw() { [ -n "$TEST_ROOT" ] || mount -uw "$1"; }
remount_ro() { [ -n "$TEST_ROOT" ] || mount -ur "$1" 2>/dev/null; }
sync_all() { [ -n "$TEST_ROOT" ] || sync; }
fail() { echo "FAILED: $*"; exit 1; }

# Print only audio.sink.media through the start of nvaudio. This is narrow on
# purpose: gal.json has other audio blocks with their own rate/bit fields.
media_block() {
    sed -n '/^[ 	]*"media":{[ 	]*$/,/^[ 	]*"nvaudio":{[ 	]*$/p' "$1" 2>/dev/null
}

media_count() {
    media_block "$1" | grep -c "^[ 	]*\"$2\":$3,[ 	]*$"
}

state_of() {
    file=$1
    [ -f "$file" ] || { echo missing; return; }
    [ "$(grep -c '^[ 	]*"media":{[ 	]*$' "$file" 2>/dev/null)" = 1 ] || { echo ambiguous; return; }
    [ "$(media_count "$file" freq 48000)" = 1 ] || { echo unsupported; return; }
    [ "$(media_count "$file" channels 2)" = 1 ] || { echo unsupported; return; }
    [ "$(media_count "$file" codecType 1)" = 1 ] || { echo unsupported; return; }
    b16=$(media_count "$file" bits 16)
    b24=$(media_count "$file" bits 24)
    if [ "$b16" = 1 ] && [ "$b24" = 0 ]; then echo pcm48_s16_stereo
    elif [ "$b16" = 0 ] && [ "$b24" = 1 ]; then echo pcm48_s24_stereo
    else echo ambiguous; fi
}

# Change exactly the media-block bit-depth line. A reverse transformation must
# reproduce the source byte-for-byte before the result is eligible to install.
edit_bits() {
    src=$1 dst=$2 from=$3 to=$4
    sed '/^[ 	]*"media":{[ 	]*$/,/^[ 	]*"nvaudio":{[ 	]*$/s/^\([ 	]*\)"bits":'"$from"',/\1"bits":'"$to"',/' \
        "$src" > "$dst" || return 1
    [ "$(media_count "$dst" bits "$to")" = 1 ] || return 1
    [ "$(media_count "$dst" bits "$from")" = 0 ] || return 1
    round=$dst.round.$$
    sed '/^[ 	]*"media":{[ 	]*$/,/^[ 	]*"nvaudio":{[ 	]*$/s/^\([ 	]*\)"bits":'"$to"',/\1"bits":'"$from"',/' \
        "$dst" > "$round" || return 1
    cmp -s "$src" "$round"; rc=$?
    rm -f "$round"
    return $rc
}

put_verified() {
    src=$1 dst=$2 tmp=$dst.sq5audio-new.$$
    cp -p "$src" "$tmp" || return 1
    cmp -s "$src" "$tmp" || { rm -f "$tmp"; return 1; }
    mv -f "$tmp" "$dst" || { rm -f "$tmp"; return 1; }
    cmp -s "$src" "$dst"
}

collect_logs() {
    stamp=$(date +%Y%m%d_%H%M%S 2>/dev/null || echo unknown)
    run=$SD/mib2q-aa-audio-probe/run-$stamp-$$
    mkdir -p "$run" 2>/dev/null || return 0
    echo "gal_json_state=$(state_of "$GJ")" > "$run/summary.txt"
    echo "gal_json_cksum=$(sum_of "$GJ")" >> "$run/summary.txt"
    echo "backup_present=$([ -f "$BAK" ] && echo yes || echo no)" >> "$run/summary.txt"
    [ -f "$GJ" ] && cp "$GJ" "$run/gal.json.now" 2>/dev/null
    for src in "$(rooted /tmp)/mib2q_aa_startup.log" \
               "$(rooted /tmp)/sq5_cluster_player.log" \
               "$(rooted /tmp)"/sq5_cluster_live-hook-*.log; do
        [ -f "$src" ] && cp "$src" "$run/${src##*/}" 2>/dev/null
    done
    if [ -z "$TEST_ROOT" ]; then
        sloginfo > "$run/mmx_sloginfo.txt" 2>&1 || true
        pidin ar > "$run/mmx_pidin_ar.txt" 2>&1 || true
    fi
    echo "Saved Android Auto audio evidence to $run"
}

enable_24() {
    current=$(state_of "$GJ")
    [ "$current" = pcm48_s16_stereo ] || {
        [ "$current" = pcm48_s24_stereo ] && { echo "Already requesting PCM 48 kHz/24-bit/stereo."; return; }
        fail "gal.json media format is $current; refusing to edit"
    }
    [ ! -f "$BAK" ] || fail "backup already exists while current file is stock; restore/audit manually"
    work=$SD/mib2q-aa-audio-probe/gal.json.pcm24.$$
    mkdir -p "${work%/*}" || fail "create SD work directory"
    edit_bits "$GJ" "$work" 16 24 || fail "prepare a byte-reversible 24-bit gal.json"
    [ "$(state_of "$work")" = pcm48_s24_stereo ] || fail "verify 24-bit candidate"
    remount_rw "$SYS" || fail "remount /mnt/system read-write"
    cp -p "$GJ" "$BAK" && cmp -s "$GJ" "$BAK" || {
        rm -f "$BAK"; remount_ro "$SYS" || true; fail "create exact unit backup";
    }
    if ! put_verified "$work" "$GJ"; then
        put_verified "$BAK" "$GJ" || true
        remount_ro "$SYS" || true
        fail "install 24-bit candidate; attempted immediate rollback"
    fi
    sync_all
    remount_ro "$SYS" || true
    rm -f "$work"
    [ "$(state_of "$GJ")" = pcm48_s24_stereo ] || fail "post-write verification"
    echo "ENABLED negotiation probe: PCM 48 kHz/24-bit/stereo. Reboot the MMI."
    echo "This is not proof of 24-bit playback; rerun this script after the test to collect logs."
}

restore_stock() {
    current=$(state_of "$GJ")
    if [ ! -f "$BAK" ]; then
        [ "$current" = pcm48_s16_stereo ] && { echo "Already restored; no probe backup exists."; return; }
        fail "no exact probe backup exists (current state $current)"
    fi
    [ "$(state_of "$BAK")" = pcm48_s16_stereo ] || fail "backup is not the pre-probe PCM16 configuration"
    remount_rw "$SYS" || fail "remount /mnt/system read-write"
    put_verified "$BAK" "$GJ" || { remount_ro "$SYS" || true; fail "restore exact backup"; }
    cmp -s "$BAK" "$GJ" || { remount_ro "$SYS" || true; fail "restored file differs from backup"; }
    rm -f "$BAK"
    sync_all
    remount_ro "$SYS" || true
    echo "RESTORED exact pre-probe gal.json. Reboot the MMI."
}

echo "MIB2Q Android Auto audio negotiation probe"
echo "  current media format: $(state_of "$GJ")"
echo "  gal.json:             $(sum_of "$GJ")"
echo "  backup:               $([ -f "$BAK" ] && echo present || echo absent)"
collect_logs

actions=0
[ -f "$ON" ] && actions=$((actions+1))
[ -f "$ONCE" ] && actions=$((actions+1))
[ -f "$RESTORE" ] && actions=$((actions+1))
[ "$actions" -le 1 ] || fail "multiple audio-probe action markers exist; keep only one"

if [ -f "$ONCE" ]; then
    current=$(state_of "$GJ")
    if [ "$current" = pcm48_s16_stereo ] && [ ! -f "$BAK" ]; then
        enable_24
        echo "ONE-SHOT ARMED: after the drive, rerun this script to collect evidence and restore stock."
    elif [ "$current" = pcm48_s24_stereo ] && [ -f "$BAK" ]; then
        restore_stock
        echo "ONE-SHOT COMPLETE: evidence was saved before restoration. Reboot the MMI."
    else
        fail "one-shot state is inconsistent (media=$current backup=$([ -f "$BAK" ] && echo yes || echo no))"
    fi
elif [ -f "$RESTORE" ]; then
    restore_stock
elif [ -f "$ON" ]; then
    enable_24
else
    echo "AUDIT ONLY: no action marker; unit unchanged."
fi
