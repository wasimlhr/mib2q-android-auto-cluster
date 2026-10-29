#!/bin/sh
# M.I.B. release builds execute command.sh; current builds execute custom.sh.
# The M.I.B. installer also sources this file, in which case $0 is its launcher
# and this forwarder must do nothing.
case ${0##*/} in
    command.sh) exec /bin/sh "${0%/*}/custom.sh" "$@" ;;
esac
