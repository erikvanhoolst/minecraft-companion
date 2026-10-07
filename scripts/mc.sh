#!/bin/sh
# Thin wrappers around duo.py / probe.py with the right environment.
#   mc.sh cmd <console command...>   mc.sh btn "A 150" "wait 3000" ...   mc.sh shot <name>
#   mc.sh value <name> [...]         mc.sh vt <Class>                     mc.sh dump <hexaddr> [len]
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
PY="${PYTHON:-python3}"
export MC_RUN="${MC_RUN:-/tmp/mc}"
case "$1" in
  value) shift; for v in "$@"; do timeout 60 "$PY" "$HERE/duo.py" cmd value "$v" | tail -1; done ;;
  vt|dump|chain|base) timeout 300 "$PY" "$HERE/probe.py" "$@" ;;
  *) timeout 300 "$PY" "$HERE/duo.py" "$@" ;;
esac
