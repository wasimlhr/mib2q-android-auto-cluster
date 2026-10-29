#!/bin/sh
# One-purpose MU1201 test launcher: keep Android Auto service 1 on the centre
# screen and copy its rendered window 59 into cockpit window 99.  The normal
# independent cluster player is intentionally not started, so two renderers
# never compete for window 99.
set +u
PATH=/proc/boot:/bin:/usr/bin:/usr/sbin:/sbin:/mnt/app/armle/bin:/mnt/app/armle/usr/bin
export PATH
BASE=/mnt/app/root/sq5_android_auto
GAL=/mnt/app/eso/bin/apps/gal
LOG=/tmp/mib2q_aa_startup.log
OWNER=/tmp/sq5_luka_supervisor.owner

sd_flag() {
    for root in /fs/sda0 /net/mmx/fs/sda0; do
        [ -f "$root/$1" ] && return 0
    done
    return 1
}

fallback() {
    reason=$1
    shift
    echo "stock GAL: $reason" >> "$LOG"
    unset LD_PRELOAD SQ5_CLUSTER_PROBE SQ5_AA_UPMIX SQ5_AA_UPMIX_MASK SQ5_AA_UPMIX_LEVEL
    exec "$GAL" "$@"
}

echo "Naver mirror test launch pid=$$ args=$*" >> "$LOG"
for flag in mib2q_aa_off sq5_hook_off; do
    sd_flag "$flag" && fallback "disabled by $flag" "$@"
done

unset SQ5_AA_UPMIX SQ5_AA_UPMIX_MASK SQ5_AA_UPMIX_LEVEL
if sd_flag sq5_aa_upmix_probe; then
    export SQ5_AA_UPMIX=probe
elif sd_flag sq5_aa_upmix; then
    [ -r "$BASE/scene_masknet_c.bin" ] || fallback "missing scene_masknet_c.bin" "$@"
    export SQ5_AA_UPMIX=on
    export SQ5_AA_UPMIX_MASK=$BASE/scene_masknet_c.bin
    export SQ5_AA_UPMIX_LEVEL=2
    for root in /fs/sda0 /net/mmx/fs/sda0; do
        if [ -f "$root/sq5_aa_upmix_level" ]; then
            read level < "$root/sq5_aa_upmix_level" || level=
            case $level in 0|1|2|3|4) SQ5_AA_UPMIX_LEVEL=$level;; esac
            break
        fi
    done
fi

for file in libsq5_cluster_live.so libsq5_main_mirror.so sq5_naver_mirror \
            sq5_naver_mirror_supervisor.sh maneuver_render \
            sq5_luka_monitor.sh sq5_luka_processes.sh; do
    [ -r "$BASE/$file" ] || fallback "missing $file" "$@"
done

# Preserve the established maneuver/HUD lifecycle.  It sees the normal cluster
# installation and therefore does not start the legacy sq5_mirror itself.
echo $$ > "$OWNER.$$" && mv "$OWNER.$$" "$OWNER"
H=$BASE WLOG=/tmp/sq5_luka_wrapper.log OWNER_FILE=$OWNER LD_PRELOAD= \
    "$BASE/sq5_luka_monitor.sh" "$$" </dev/null >>/tmp/sq5_luka_wrapper.log 2>&1 &

# This supervisor owns the only renderer for window 99 during the experiment.
LD_PRELOAD= "$BASE/sq5_naver_mirror_supervisor.sh" "$$" </dev/null \
    >/tmp/sq5_naver_mirror_supervisor.log 2>&1 &

export SQ5_CLUSTER_PROBE=1
export LD_PRELOAD=$BASE/libsq5_main_mirror.so:$BASE/libsq5_cluster_live.so
echo "Naver mirror enabled; supervisor=$! preload=$LD_PRELOAD aa_upmix=${SQ5_AA_UPMIX:-off} level=${SQ5_AA_UPMIX_LEVEL:-n/a}" >> "$LOG"
exec "$GAL" "$@"
