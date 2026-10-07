#!/bin/sh
# dbg.sh "<cmd>" ["<cmd>" ...]: run research-console commands (native/mc_debug.h) in the module of
# the running desktop game and print the result lines from the Eden log.
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
[ -f "$ROOT/local.env" ] && . "$ROOT/local.env"
DATA="${EDEN_DATA:-$HOME/.local/share/eden}"
U=$DATA/dualscreen/user/0100D71004694000; mkdir -p "$U"
L=$DATA/log/eden_log.txt
before=$(wc -l < "$L")
{ for c in "$@"; do printf '%s\n' "$c"; done; printf '# %s\n' "$(date +%s%N)"; } > "$U/mcdbg.txt.tmp"; mv "$U/mcdbg.txt.tmp" "$U/mcdbg.txt"
for i in $(seq 1 ${MC_DBG_TIMEOUT:-400}); do sleep 1; if tail -n +$((before+1)) "$L" | grep -q 'mcdbg: done'; then break; fi; done
tail -n +$((before+1)) "$L" | grep 'mcdbg' | sed -E 's/^.*mcdbg: //'
