#!/bin/sh
# Launch Minecraft in the Eden Duo desktop build with the live console and a headless second
# screen. Usage: run_desktop.sh [--aux-window] ; MC_RUN selects the run dir (default /tmp/mc).
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
[ -f "$ROOT/local.env" ] && . "$ROOT/local.env"
EDEN="${EDEN_CLI:?set EDEN_CLI in local.env}"
GAME="${MC_GAME:?set MC_GAME in local.env}"
RUN="${MC_RUN:-/tmp/mc}"
MODE="${1:---aux-virtual}"
mkdir -p "$RUN"
rm -f "$RUN/cmd.in" "$RUN/cmd.in.out" "$RUN/p.btn" "$RUN/p.shot"
: > "$RUN/cmd.in.out"
export DISPLAY="${DISPLAY:-:0}" WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}" XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
[ -n "$MC_VK_ICD" ] && export VK_DRIVER_FILES="$MC_VK_ICD" VK_ICD_FILENAMES="$MC_VK_ICD"
export MC_ICON_DUMP="${MC_ICON_DUMP:-}"
export EDEN_VSYNC=0 EDEN_DSMOD_CMD="$RUN/cmd.in" EDEN_DSMOD_PROFILE=0
echo "eden-cli: $EDEN"; echo "game: $GAME"; echo "run dir: $RUN"; echo "mode: $MODE"
exec "$EDEN" "$MODE" --screenshot-prefix "$RUN/p" --filter "${MC_FILTER:-*:Info}" -u 0 "$GAME" > "$RUN/stdout.log" 2>&1
