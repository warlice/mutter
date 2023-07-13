#!/usr/bin/env sh
set -xe

procs=""
trap '{ kill -9 $procs 2>/dev/null || true; }' EXIT

while [ "$#" -gt 1 ]; do
    arg="$1"
    shift
    case "$arg" in
        --)
          break;
        ;;
        *)
          if [ "$MUTTER_TEST_SETUP" = "plain" ]; then
            echo "Using plain setup, NOT launching wrapper '$arg'" >&2
            continue;
          fi

          $arg &
          procs="$! $procs"
          sleep 1
        ;;
    esac
done

"$@"
