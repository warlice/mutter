#!/usr/bin/env bash

set -e

XVFB="$1"
shift

XVFB_ARGS=""

while [ "$#" -gt 0 ]; do
    arg="$1"
    shift
    case "$arg" in
        --)
          break;
        ;;
        *)
          XVFB_ARGS="$XVFB_ARGS $arg"
        ;;
    esac
done

while true; do
  dpy_number=$RANDOM

  if ! [ -e "/tmp/.X$dpy_number-lock" ]; then
    break;
  fi
done

echo "Using display $dpy_number" 2> /dev/stderr

exec "$XVFB" $XVFB_ARGS -n "$dpy_number" "$@"
