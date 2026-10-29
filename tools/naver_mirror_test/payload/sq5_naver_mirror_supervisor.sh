#!/bin/sh
# Restart the main-display mirror when GAL/window 59 is not ready yet, and stop
# it when the owning GAL generation exits.  Only this process starts the mirror.
set +u
BASE=/mnt/app/root/sq5_android_auto
OWNER=${1:-}
MIRROR=$BASE/sq5_naver_mirror
PIDFILE=/tmp/sq5_naver_mirror.pid
READY=/tmp/sq5_mirror_ready
STOP=/tmp/sq5_mirror_stop
CHILD=

alive() { [ -n "$1" ] && kill -0 "$1" 2>/dev/null; }

cleanup() {
    if alive "$CHILD"; then
        kill "$CHILD" 2>/dev/null || true
        wait "$CHILD" 2>/dev/null || true
    fi
    rm -f "$PIDFILE" "$READY" "$STOP"
}
trap cleanup 0 1 2 15

case "$OWNER" in ''|*[!0-9]*) echo "invalid GAL owner: $OWNER"; exit 2;; esac
[ -x "$MIRROR" ] || { echo "missing mirror: $MIRROR"; exit 3; }

echo "supervisor start owner=$OWNER"
while alive "$OWNER"; do
    rm -f "$STOP" "$READY"
    LD_PRELOAD= \
    LD_LIBRARY_PATH=/mnt/app/root/lib-target:/eso/lib:/mnt/app/usr/lib:/mnt/app/armle/lib:/mnt/app/armle/lib/dll:/mnt/app/armle/usr/lib \
        "$MIRROR" --display 99 --size 1440x455 --fps 30 --maps-gate off --turncard off --masks off \
        >>/tmp/sq5_naver_mirror.log 2>&1 &
    CHILD=$!
    echo "$CHILD" > "$PIDFILE"
    echo "mirror start pid=$CHILD"
    while alive "$OWNER" && alive "$CHILD"; do sleep 2; done
    if ! alive "$OWNER"; then break; fi
    wait "$CHILD" 2>/dev/null || true
    echo "mirror exited; retry in 3 seconds"
    CHILD=
    sleep 3
done
echo "owner exited"
exit 0
