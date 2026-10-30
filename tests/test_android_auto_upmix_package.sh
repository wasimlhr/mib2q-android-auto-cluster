#!/bin/sh
# Fake-root tests for install_AndroidAutoUpmix_MoreIncredibleBash.
#   tests/test_android_auto_upmix_package.sh <staged-card> <stock-080> <stock-084>
set -u
CARD=${1:?staged card}; S080=${2:?stock 0.80.0}; S084=${3:?stock 0.84.0}
SCRIPT=$CARD/install_AndroidAutoUpmix_MoreIncredibleBash/mod/custom.sh
PASS=0
ok() { PASS=$((PASS+1)); }
die() { echo "FAIL: $*"; exit 1; }
sumf() { set -- $(cksum "$1"); echo "$1 $2"; }

setup() {   # $1 = stock driver
    T=$(mktemp -d)
    mkdir -p "$T/fs/sda0" "$T/tmp" "$T/mnt/app/armle/lib/dll" "$T/mnt/app/root/sq5_android_auto"
    cp "$1" "$T/mnt/app/armle/lib/dll/deva-ctrl-qc.so"
    B=$T/mnt/app/root/sq5_android_auto
    echo mhi2q_kr_aug22_p1201_mu1201 > "$B/profile.id"
    echo old-hook > "$B/libsq5_cluster_live.so"
    echo old-startup > "$B/gal_startup.sh"
    D=$T/mnt/app/armle/lib/dll/deva-ctrl-qc.so
}
run() { MIB2Q_AA_TEST_ROOT=$T MIB2Q_AA_TEST_AMP=${AMP:-external} sh "$SCRIPT" > "$T/out.txt" 2>&1; }

for case in 084 080; do
    if [ $case = 084 ]; then STOCKF=$S084; MIRROR='15480989 694904'; else STOCKF=$S080; MIRROR='380902017 690332'; fi
    STOCK=$(sumf "$STOCKF")

    setup "$STOCKF"
    run || die "$case audit"; grep -q "AUDIT ONLY" "$T/out.txt" || die "$case audit text"
    [ "$(sumf "$D")" = "$STOCK" ] || die "$case audit changed driver"; ok

    : > "$T/fs/sda0/mib2q_aa_upmix_install"
    AMP=internal run && die "$case internal amp accepted"
    grep -q "amplifier coding is internal" "$T/out.txt" || die "$case internal text"
    [ "$(sumf "$D")" = "$STOCK" ] || die "$case internal changed driver"; ok
    AMP=unknown run && die "$case unknown amp accepted"; ok

    run || { cat "$T/out.txt"; die "$case install"; }
    [ "$(sumf "$D")" = "$MIRROR" ] || die "$case driver not mirrored"
    [ -f "$B/aa_upmix_enabled" ] || die "$case no enable flag"
    [ "$(sumf "$B/libsq5_cluster_live.so")" = '1798652718 1042903' ] || die "$case hook not replaced"
    grep -q aa_upmix_enabled "$B/gal_startup.sh" || die "$case startup not replaced"
    [ "$(sumf "$B/upmix-backup/deva-ctrl-qc.so.stock")" = "$STOCK" ] || die "$case driver backup"
    [ "$(cat "$B/upmix-backup/libsq5_cluster_live.so")" = old-hook ] || die "$case hook backup"; ok

    run || die "$case reinstall"; [ "$(sumf "$D")" = "$MIRROR" ] || die "$case reinstall driver"
    [ "$(cat "$B/upmix-backup/libsq5_cluster_live.so")" = old-hook ] || die "$case backup overwritten"; ok

    : > "$T/fs/sda0/mib2q_aa_upmix_remove"
    run && die "$case both markers accepted"; ok
    rm -f "$T/fs/sda0/mib2q_aa_upmix_install"
    run || { cat "$T/out.txt"; die "$case remove"; }
    [ "$(sumf "$D")" = "$STOCK" ] || die "$case driver not restored"
    [ ! -f "$B/aa_upmix_enabled" ] || die "$case flag left"
    [ "$(cat "$B/libsq5_cluster_live.so")" = old-hook ] || die "$case hook not restored"
    [ "$(cat "$B/gal_startup.sh")" = old-startup ] || die "$case startup not restored"
    [ ! -d "$B/upmix-backup" ] || die "$case backup left"; ok
    run && die "$case second remove accepted"; ok
    rm -rf "$T"
done

# Unknown driver build: refused, copied to the card for porting.
setup "$SCRIPT"
: > "$T/fs/sda0/mib2q_aa_upmix_install"
run && die "unknown driver accepted"
grep -q "not a supported stock build" "$T/out.txt" || die "unknown driver text"
ls "$T"/fs/sda0/mib2q-aa-logs/upmix-*/deva-ctrl-qc.so.unit >/dev/null 2>&1 || die "unknown driver not collected"
[ ! -f "$B/aa_upmix_enabled" ] || die "unknown driver enabled"; ok
rm -rf "$T"

# Cluster mod missing: refused.
setup "$S084"; rm -f "$B/profile.id"
: > "$T/fs/sda0/mib2q_aa_upmix_install"
run && die "install without cluster mod accepted"
[ "$(sumf "$D")" = "$(sumf "$S084")" ] || die "no-cluster changed driver"; ok
rm -rf "$T"

echo "android_auto_upmix_package: $PASS checks PASS"
