#!/bin/sh
# MU1201 Naver Map main-display mirror experiment.
#
# Default action is read-only audit + log collection.  Installation and removal
# require an explicit marker on the SD-card root:
#   naver_mirror_test_install
#   naver_mirror_test_remove
set -u
PATH=/proc/boot:/bin:/usr/bin:/usr/sbin:/sbin:/mnt/app/armle/bin:/mnt/app/armle/usr/bin
export PATH
unset LD_PRELOAD

case $0 in */*) MOD=${0%/*} ;; *) MOD=. ;; esac
MOD=$(cd "$MOD" && pwd) || exit 1
PAYLOAD=$MOD/naver-mirror-test
TEST_ROOT=${NAVER_MIRROR_TEST_ROOT:-}

if [ -z "$TEST_ROOT" ] && [ ! -d /mnt/app/eso/bin/apps ] && [ -d /net/mmx/mnt/app/eso/bin/apps ]; then
    exec on -f mmx /bin/sh "$MOD/custom.sh" "$@"
fi

rooted() { if [ -n "$TEST_ROOT" ]; then echo "$TEST_ROOT$1"; else echo "$1"; fi; }
SD=$(rooted /fs/sda0)
[ -d "$SD" ] || SD=$(rooted /net/mmx/fs/sda0)
BASE=$(rooted /mnt/app/root/sq5_android_auto)
BACKUP=$BASE/naver-mirror-test-backup
STARTUP=$BASE/gal_startup.sh
PROFILE=$BASE/profile.id
LOGROOT=$SD/naver-mirror-test-logs

sum_of() { set -- $(cksum "$1" 2>/dev/null); echo "${1:-0} ${2:-0}"; }
fail() { echo "FAILED: $*"; exit 1; }
remount_rw() { [ -n "$TEST_ROOT" ] || mount -uw "$1"; }
remount_ro() { [ -n "$TEST_ROOT" ] || mount -ur "$1" 2>/dev/null; }
sync_all() { [ -n "$TEST_ROOT" ] || sync; }

put() {
    src=$1
    dst=$2
    mode=$3
    tmp=$dst.naver-new.$$
    cp -p "$src" "$tmp" || return 1
    chmod "$mode" "$tmp" || { rm -f "$tmp"; return 1; }
    [ "$(sum_of "$src")" = "$(sum_of "$tmp")" ] || { rm -f "$tmp"; return 1; }
    mv -f "$tmp" "$dst"
}

verify_payload() {
    [ -f "$PAYLOAD/manifest.cksum" ] || fail "missing payload manifest"
    count=0
    while read crc bytes name extra; do
        [ -n "${name:-}" ] || continue
        [ -z "${extra:-}" ] || fail "malformed manifest entry $name"
        case "$name" in
            libsq5_main_mirror.so|sq5_naver_mirror|gal_startup_naver_mirror.sh|sq5_naver_mirror_supervisor.sh) : ;;
            *) fail "unexpected payload entry $name" ;;
        esac
        [ "$(sum_of "$PAYLOAD/$name")" = "$crc $bytes" ] || fail "payload checksum $name"
        count=$((count+1))
    done < "$PAYLOAD/manifest.cksum"
    [ "$count" = 4 ] || fail "manifest has $count files, expected 4"
}

installed_state() {
    [ -f "$BACKUP/gal_startup.original.sh" ] &&
    [ -f "$BASE/libsq5_main_mirror.so" ] &&
    [ -f "$BASE/sq5_naver_mirror" ] &&
    [ -f "$BASE/sq5_naver_mirror_supervisor.sh" ] &&
    [ -f "$STARTUP" ] &&
    [ "$(sum_of "$STARTUP")" = "$(sum_of "$PAYLOAD/gal_startup_naver_mirror.sh")" ]
}

audit() {
    echo "MU1201 Naver main-display mirror test - audit"
    echo "  profile:              $(cat "$PROFILE" 2>/dev/null || echo missing)"
    echo "  Android Auto base:    $BASE"
    echo "  current startup:      $(sum_of "$STARTUP")"
    if installed_state; then
        echo "  test state:           INSTALLED (reboot required after any change)"
    elif [ -d "$BACKUP" ]; then
        echo "  test state:           INCOMPLETE OR STARTUP CHANGED - do not overwrite"
    else
        echo "  test state:           not installed"
    fi
    echo "  normal cluster hook:  $(sum_of "$BASE/libsq5_cluster_live.so")"
    echo "  normal cluster player: $(sum_of "$BASE/cluster-player")"
}

collect_logs() {
    tmp=$(rooted /tmp)
    stamp=$(date +%Y%m%d_%H%M%S 2>/dev/null || echo unknown)
    run=$LOGROOT/run-$stamp-$$
    count=0
    mkdir -p "$run" || { echo "WARNING: cannot create $run"; return; }
    for src in "$tmp"/mib2q_aa_launcher.log "$tmp"/mib2q_aa_startup.log \
               "$tmp"/sq5_naver_mirror.log "$tmp"/sq5_naver_mirror_supervisor.log \
               "$tmp"/sq5_galhook.log "$tmp"/sq5_luka_wrapper.log \
               "$tmp"/sq5_aa.log "$tmp"/luka_sq5_aa.log \
               "$tmp"/maneuver_render.log "$tmp"/sq5_cluster_live-hook-*.log; do
        [ -f "$src" ] || continue
        cp "$src" "$run/${src##*/}" 2>/dev/null && count=$((count+1))
    done
    {
        echo "runtime_logs=$count"
        echo "profile=$(cat "$PROFILE" 2>/dev/null || echo missing)"
        echo "startup=$(sum_of "$STARTUP")"
        echo "test_startup=$(sum_of "$PAYLOAD/gal_startup_naver_mirror.sh")"
        echo "backup_startup=$(sum_of "$BACKUP/gal_startup.original.sh")"
        for flag in mib2q_aa_off sq5_hook_off sq5_cluster_live_off sq5_cluster_noui sq5_mirror_off sq5_mirror_map; do
            state=absent
            [ -f "$SD/$flag" ] && state=present
            echo "flag.$flag=$state"
        done
        ls -l "$BASE" 2>/dev/null || true
        echo "--- processes ---"
        pidin ar 2>/dev/null | grep -E "gal|sq5_naver_mirror|maneuver_render" || true
    } > "$run/status.txt"
    cp "$STARTUP" "$run/gal_startup.active.sh" 2>/dev/null || true
    cp "$BACKUP/gal_startup.original.sh" "$run/gal_startup.original.sh" 2>/dev/null || true
    echo "Saved $count runtime log(s) to $run"
}

preflight_unit() {
    [ -d "$BASE" ] || fail "Android Auto package is not installed at $BASE"
    [ -f "$STARTUP" ] || fail "missing $STARTUP"
    [ -f "$BASE/libsq5_cluster_live.so" ] || fail "missing normal cluster hook"
    [ -f "$BASE/maneuver_render" ] || fail "missing maneuver renderer"
    [ -f "$BASE/sq5_luka_monitor.sh" ] || fail "missing LuKa monitor"
    [ -f "$BASE/sq5_luka_processes.sh" ] || fail "missing LuKa process helpers"
    profile=$(cat "$PROFILE" 2>/dev/null || echo missing)
    [ "$profile" = "mhi2q_kr_aug22_p1201_mu1201" ] || fail "requires exact MU1201 profile; found $profile"
}

install_test() {
    verify_payload
    preflight_unit
    if installed_state; then
        echo "ALREADY INSTALLED. Remove naver_mirror_test_install and reboot only if not already rebooted."
        return
    fi
    [ ! -d "$BACKUP" ] || fail "backup already exists but installed state is inconsistent"
    [ ! -f "$BASE/libsq5_main_mirror.so" ] && [ ! -d "$BASE/libsq5_main_mirror.so" ] || fail "libsq5_main_mirror.so already exists"
    [ ! -f "$BASE/sq5_naver_mirror" ] && [ ! -d "$BASE/sq5_naver_mirror" ] || fail "sq5_naver_mirror already exists"
    [ ! -f "$BASE/sq5_naver_mirror_supervisor.sh" ] && [ ! -d "$BASE/sq5_naver_mirror_supervisor.sh" ] || fail "mirror supervisor already exists"
    grep -q 'libsq5_cluster_live.so' "$STARTUP" || fail "startup is not the expected cluster launcher"
    grep -q 'cluster-player' "$STARTUP" || fail "startup does not contain the expected independent player"

    remount_rw /mnt/app || fail "remount /mnt/app read-write"
    trap 'remount_ro /mnt/app || true' 0
    mkdir -p "$BACKUP" || fail "create backup directory"
    cp -p "$STARTUP" "$BACKUP/gal_startup.original.sh" || fail "backup current startup"
    sum_of "$STARTUP" > "$BACKUP/original.cksum" || fail "record startup checksum"

    put "$PAYLOAD/libsq5_main_mirror.so" "$BASE/libsq5_main_mirror.so" 755 || fail "install main-display capture hook"
    put "$PAYLOAD/sq5_naver_mirror" "$BASE/sq5_naver_mirror" 755 || fail "install mirror renderer"
    put "$PAYLOAD/sq5_naver_mirror_supervisor.sh" "$BASE/sq5_naver_mirror_supervisor.sh" 755 || fail "install mirror supervisor"
    put "$PAYLOAD/gal_startup_naver_mirror.sh" "$STARTUP" 755 || fail "activate mirror test startup"
    sync_all
    remount_ro /mnt/app || true
    trap - 0
    echo "INSTALLED: remove naver_mirror_test_install from the SD root, then reboot the MMI."
    echo "Emergency off switch: create mib2q_aa_off on the SD root and reboot."
}

remove_test() {
    verify_payload
    [ -f "$BACKUP/gal_startup.original.sh" ] || fail "no test backup exists"
    if [ "$(sum_of "$STARTUP")" != "$(sum_of "$PAYLOAD/gal_startup_naver_mirror.sh")" ] &&
       [ "$(sum_of "$STARTUP")" != "$(sum_of "$BACKUP/gal_startup.original.sh")" ]; then
        fail "gal_startup.sh changed after test installation; refusing to overwrite it"
    fi
    remount_rw /mnt/app || fail "remount /mnt/app read-write"
    trap 'remount_ro /mnt/app || true' 0
    put "$BACKUP/gal_startup.original.sh" "$STARTUP" 755 || fail "restore original startup"
    rm -f "$BASE/libsq5_main_mirror.so" "$BASE/sq5_naver_mirror" "$BASE/sq5_naver_mirror_supervisor.sh"
    rm -f "$BACKUP/original.cksum" "$BACKUP/gal_startup.original.sh"
    rmdir "$BACKUP" 2>/dev/null || fail "backup directory contains unexpected files"
    sync_all
    remount_ro /mnt/app || true
    trap - 0
    echo "REMOVED: remove naver_mirror_test_remove from the SD root, then reboot the MMI."
}

audit
collect_logs
if [ -f "$SD/naver_mirror_test_install" ] && [ -f "$SD/naver_mirror_test_remove" ]; then
    fail "both action markers are present; keep only one"
elif [ -f "$SD/naver_mirror_test_install" ]; then
    install_test
elif [ -f "$SD/naver_mirror_test_remove" ]; then
    remove_test
else
    echo "AUDIT/COLLECT ONLY: logs were written to the SD card; no head-unit files were modified."
fi
