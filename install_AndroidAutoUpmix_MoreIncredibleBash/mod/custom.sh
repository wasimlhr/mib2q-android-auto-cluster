#!/bin/sh
# M.I.B. custom script: Android Auto 5.1 (stereo -> 5.1 upmix) for an
# already-installed mib2q-android-auto-cluster.
#
# Default action is a read-only audit. Put mib2q_aa_upmix_install or
# mib2q_aa_upmix_remove in the SD-card root to authorize that operation.
#
# Install replaces three files and backs up each one first:
#   /mnt/app/armle/lib/dll/deva-ctrl-qc.so   mpl6_gal_ent record mirrored from mpl1
#                                            (6ch, 24-bit, 768-frame periods)
#   $BASE/libsq5_cluster_live.so             hook with the PCM6 bridge and 5.1 SAD
#   $BASE/gal_startup.sh                     turns the upmix on when the driver is in
# It refuses unless the amplifier coding says external (B&O/Bose MOST amp) and
# the unit's driver is exactly one of the stock builds below.  Remove restores
# all three from the backups.
set -u
PATH=/proc/boot:/bin:/usr/bin:/usr/sbin:/sbin:/mnt/app/armle/bin:/mnt/app/armle/usr/bin
export PATH
unset LD_PRELOAD

case $0 in */*) MOD=${0%/*} ;; *) MOD=. ;; esac
MOD=$(cd "$MOD" && pwd) || exit 1
TEST_ROOT=${MIB2Q_AA_TEST_ROOT:-}

if [ -z "$TEST_ROOT" ] && [ ! -d /mnt/app/eso/bin/apps ] && [ -d /net/mmx/mnt/app/eso/bin/apps ]; then
    exec on -f mmx /bin/sh "$MOD/custom.sh" "$@"
fi

rooted() { if [ -n "$TEST_ROOT" ]; then echo "$TEST_ROOT$1"; else echo "$1"; fi; }
SD=$(rooted /fs/sda0)
[ -d "$SD" ] || SD=$(rooted /net/mmx/fs/sda0)
RES=$MOD/upmix
BASE=$(rooted /mnt/app/root/sq5_android_auto)
UBACK=$BASE/upmix-backup
DRV=$(rooted /mnt/app/armle/lib/dll/deva-ctrl-qc.so)

# deva-ctrl-qc.so builds this package can patch: stock cksum -> payload, mirror cksum.
# 0.80.0 [Aug 19 2016] is MU0918 (proven by ear on the owner's SQ5, 2026-10-29).
# 0.84.0 [Jan 30 2018] is the MU1316/MU1201 build (QEMU-verified with the real driver).
STOCK_080='3610384356 690332'; MIRROR_080='380902017 690332'
STOCK_084='325683668 694904';  MIRROR_084='15480989 694904'

sum_of() { set -- $(cksum "$1" 2>/dev/null); echo "${1:-0} ${2:-0}"; }
fail() { echo "FAILED: $*"; exit 1; }
remount_rw() { [ -n "$TEST_ROOT" ] || mount -uw "$1"; }
remount_ro() { [ -n "$TEST_ROOT" ] || mount -ur "$1" 2>/dev/null; }
sync_all() { [ -n "$TEST_ROOT" ] || sync; }

put() {
    src=$1; dst=$2; mode=$3; tmp=$dst.upmix-new.$$
    cp -p "$src" "$tmp" || return 1
    chmod "$mode" "$tmp" || { rm -f "$tmp"; return 1; }
    [ "$(sum_of "$src")" = "$(sum_of "$tmp")" ] || { rm -f "$tmp"; return 1; }
    mv -f "$tmp" "$dst"
}

# Amplifier coding, as install_MoreIncredibleBash reads it: RCC E2P 0xFC is 02
# for an external MOST amplifier and 01 for the internal one.  Only the external
# branch has a six-channel route (docs/audio/INTERNAL_AMP_PATH.md).
E2P_AMP_READ=${E2P_AMP_READ:-"on -f rcc /usr/apps/modifyE2P r FC 1"}
read_amp() {
    AMP=unknown
    if [ -n "$TEST_ROOT" ]; then AMP=${MIB2Q_AA_TEST_AMP:-unknown}; return; fi
    out=/tmp/mib2q_upmix_e2p.$$
    $E2P_AMP_READ > "$out" 2>/dev/null &
    p=$!; i=0
    while kill -0 $p 2>/dev/null && [ $i -lt 10 ]; do sleep 1; i=$((i+1)); done
    kill $p 2>/dev/null
    while read -r addr rest; do
        case $addr in 0x*|0X*) ;; *) continue ;; esac
        set -- $rest
        b=${1:-}; b=${b%"${b#??}"}
        case $b in 02) AMP=external ;; 01) AMP=internal ;; esac
        break
    done < "$out"
    rm -f "$out"
}

driver_state() {
    DRV_SUM=$(sum_of "$DRV")
    DRV_BUILD= DRV_STATE=unknown PAYLOAD= MIRROR= STOCK=
    case "$DRV_SUM" in
        "$STOCK_080")  DRV_BUILD=0.80.0; DRV_STATE=stock ;;
        "$MIRROR_080") DRV_BUILD=0.80.0; DRV_STATE=mirror ;;
        "$STOCK_084")  DRV_BUILD=0.84.0; DRV_STATE=stock ;;
        "$MIRROR_084") DRV_BUILD=0.84.0; DRV_STATE=mirror ;;
    esac
    case $DRV_BUILD in
        0.80.0) PAYLOAD=$RES/deva-ctrl-qc.so.mirror-080; MIRROR=$MIRROR_080; STOCK=$STOCK_080 ;;
        0.84.0) PAYLOAD=$RES/deva-ctrl-qc.so.mirror-084; MIRROR=$MIRROR_084; STOCK=$STOCK_084 ;;
    esac
}

verify_payload() {
    [ -f "$RES/manifest.cksum" ] || fail "missing $RES/manifest.cksum"
    n=0
    while read crc bytes name extra; do
        [ -z "${extra:-}" ] || fail "malformed manifest line for $name"
        case "$name" in
            libsq5_cluster_live.so|gal_startup.sh|deva-ctrl-qc.so.mirror-080|deva-ctrl-qc.so.mirror-084) ;;
            *) fail "unexpected manifest entry $name" ;;
        esac
        [ "$(sum_of "$RES/$name")" = "$crc $bytes" ] || fail "payload checksum $name"
        n=$((n+1))
    done < "$RES/manifest.cksum"
    [ "$n" = 4 ] || fail "manifest has $n entries, expected 4"
    [ "$(sum_of "$RES/deva-ctrl-qc.so.mirror-080")" = "$MIRROR_080" ] || fail "0.80.0 payload is not the reviewed mirror"
    [ "$(sum_of "$RES/deva-ctrl-qc.so.mirror-084")" = "$MIRROR_084" ] || fail "0.84.0 payload is not the reviewed mirror"
}

audit() {
    read_amp
    driver_state
    echo "MIB2Q Android Auto 5.1 - read-only audit"
    echo "  cluster mod:         $(cat "$BASE/profile.id" 2>/dev/null || echo 'NOT INSTALLED')"
    echo "  amplifier coding:    $AMP"
    echo "  audio driver:        $DRV_SUM (${DRV_BUILD:-unknown build}, $DRV_STATE)"
    echo "  5.1 enabled flag:    $([ -f "$BASE/aa_upmix_enabled" ] && echo yes || echo no)"
    echo "  backup:              $([ -d "$UBACK" ] && echo present || echo none)"
}

collect_logs() {
    tmp=$(rooted /tmp)
    stamp=$(date +%Y%m%d_%H%M%S 2>/dev/null || echo unknown)
    run=$SD/mib2q-aa-logs/upmix-$stamp-$$
    mkdir -p "$run" || return 0
    for src in "$tmp"/mib2q_aa_startup.log "$tmp"/sq5_cluster_live-hook-*.log; do
        [ -f "$src" ] && cp "$src" "$run/${src##*/}" 2>/dev/null
    done
    [ -n "$TEST_ROOT" ] || sloginfo > "$run/sloginfo.txt" 2>&1
    audit > "$run/status.txt" 2>&1
    # An unknown driver is copied for porting; nothing is changed on such a unit.
    driver_state
    [ "$DRV_STATE" != unknown ] || cp "$DRV" "$run/deva-ctrl-qc.so.unit" 2>/dev/null
    echo "Saved Android Auto 5.1 diagnostics to $run"
}

install_it() {
    verify_payload
    [ -f "$BASE/profile.id" ] || fail "install the Android Auto cluster mod first (mib2q_aa_install)"
    for f in libsq5_cluster_live.so gal_startup.sh; do
        [ -f "$BASE/$f" ] || fail "cluster mod file $f missing; reinstall it first"
    done
    read_amp
    [ "$AMP" = external ] || fail "amplifier coding is $AMP; 5.1 needs the external B&O/Bose MOST amplifier. Nothing changed"
    driver_state
    case $DRV_STATE in
        stock) ;;
        mirror)
            [ -f "$UBACK/deva-ctrl-qc.so.stock" ] || fail "5.1 driver present but no stock backup; refusing" ;;
        *) fail "audio driver $DRV_SUM is not a supported stock build; copied to the log folder for porting. Nothing changed" ;;
    esac

    remount_rw /mnt/app || fail "remount /mnt/app"
    mkdir -p "$UBACK" || fail "create backup directory"
    if [ ! -f "$UBACK/deva-ctrl-qc.so.stock" ]; then
        cp -p "$DRV" "$UBACK/deva-ctrl-qc.so.stock.new" || fail "back up audio driver"
        [ "$(sum_of "$UBACK/deva-ctrl-qc.so.stock.new")" = "$STOCK" ] || fail "driver backup verification"
        mv -f "$UBACK/deva-ctrl-qc.so.stock.new" "$UBACK/deva-ctrl-qc.so.stock"
    fi
    [ "$(sum_of "$UBACK/deva-ctrl-qc.so.stock")" = "$STOCK" ] || fail "driver backup is not exact stock"
    for f in libsq5_cluster_live.so gal_startup.sh; do
        [ -f "$UBACK/$f" ] || cp -p "$BASE/$f" "$UBACK/$f" || fail "back up $f"
    done

    put "$RES/libsq5_cluster_live.so" "$BASE/libsq5_cluster_live.so" 755 || fail "install hook"
    put "$RES/gal_startup.sh" "$BASE/gal_startup.sh" 755 || fail "install GAL startup"
    if [ "$DRV_STATE" = stock ]; then
        put "$PAYLOAD" "$DRV" 755 || fail "install 5.1 audio driver"
    fi
    [ "$(sum_of "$DRV")" = "$MIRROR" ] || fail "installed driver verification"
    : > "$BASE/aa_upmix_enabled" || fail "enable 5.1"
    sync_all
    remount_ro /mnt/app || true
    echo "INSTALLED Android Auto 5.1 (driver $DRV_BUILD). Remove mib2q_aa_upmix_install, then reboot the MMI."
    echo "Put sq5_aa_upmix_off on the card to hear stock stereo for one boot."
}

remove_it() {
    [ -d "$UBACK" ] || fail "no Android Auto 5.1 backup found; nothing to remove"
    driver_state
    remount_rw /mnt/app || fail "remount /mnt/app"
    rm -f "$BASE/aa_upmix_enabled"
    if [ "$DRV_STATE" = mirror ]; then
        [ "$(sum_of "$UBACK/deva-ctrl-qc.so.stock")" = "$STOCK" ] || fail "driver backup is not exact stock; refusing"
        put "$UBACK/deva-ctrl-qc.so.stock" "$DRV" 755 || fail "restore audio driver"
        [ "$(sum_of "$DRV")" = "$STOCK" ] || fail "restored driver verification"
    elif [ "$DRV_STATE" != stock ]; then
        fail "audio driver $DRV_SUM changed since install; refusing to overwrite it"
    fi
    for f in libsq5_cluster_live.so gal_startup.sh; do
        [ ! -f "$UBACK/$f" ] || put "$UBACK/$f" "$BASE/$f" 755 || fail "restore $f"
    done
    rm -rf "$UBACK"
    sync_all
    remount_ro /mnt/app || true
    echo "REMOVED Android Auto 5.1; stock audio driver restored. Remove mib2q_aa_upmix_remove, then reboot the MMI."
}

audit
collect_logs
if [ -f "$SD/mib2q_aa_upmix_install" ] && [ -f "$SD/mib2q_aa_upmix_remove" ]; then
    fail "both action markers are present; keep only one"
elif [ -f "$SD/mib2q_aa_upmix_install" ]; then
    install_it
elif [ -f "$SD/mib2q_aa_upmix_remove" ]; then
    remove_it
else
    echo "AUDIT ONLY: no action marker is present; nothing was modified."
fi
