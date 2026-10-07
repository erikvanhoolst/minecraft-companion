#!/bin/sh
# Thor (AYN Thor, Eden Duo) helpers over adb.
#   thor.sh install            put dist/0100D71004694000.dsmod.zip into Eden Duo's load folder
#                              (the in-app installer works too: Add-ons, Install, Dual screen mods)
#   thor.sh dbg "<cmd>" ...    write research-console commands to the user folder (module reads user:mcdbg.txt)
#   thor.sh log [n]            pull the Eden Duo log and show the last n lines
#   thor.sh mcdbg              pull the log and show the research-console output
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
[ -f "$ROOT/local.env" ] && . "$ROOT/local.env"
ADB="${ADB:-adb}"
LOGCOPY="${TMPDIR:-/tmp}/thor_eden_log.txt"
APP=/sdcard/Android/data/dev.igawa6.edenduo/files
TITLE=0100D71004694000
case "$1" in
  install)
    tmp=$(mktemp -d); python3 -c "import sys,zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])" "$ROOT/dist/$TITLE.dsmod.zip" "$tmp"
    rm -rf "$tmp/dualscreen/modules/linux-x86_64"
    "$ADB" shell "mkdir -p $APP/load/$TITLE/Minecraft"
    "$ADB" shell "rm -rf $APP/load/$TITLE/Minecraft/dualscreen $APP/load/$TITLE/Minecraft/package.json"
    # adb cannot create directories under Android/data itself; make them with the shell and push file by file.
    "$ADB" shell "mkdir -p $APP/load/$TITLE/Minecraft/dualscreen/modules/android-arm64-v8a"
    "$ADB" push "$tmp/package.json" "$APP/load/$TITLE/Minecraft/package.json" >/dev/null
    "$ADB" push "$tmp/dualscreen/manifest.json" "$APP/load/$TITLE/Minecraft/dualscreen/manifest.json" >/dev/null
    "$ADB" push "$tmp/dualscreen/modules/android-arm64-v8a/$TITLE.so" "$APP/load/$TITLE/Minecraft/dualscreen/modules/android-arm64-v8a/$TITLE.so" >/dev/null
    "$ADB" shell "ls -laR $APP/load/$TITLE/Minecraft" | head -20; rm -rf "$tmp" ;;
  dbg)
    shift; tmp=$(mktemp); for c in "$@"; do printf '%s\n' "$c"; done >> "$tmp"; printf '# %s\n' "$(date +%s%N)" >> "$tmp"
    "$ADB" shell "mkdir -p $APP/dualscreen/user/$TITLE"
    "$ADB" push "$tmp" "$APP/dualscreen/user/$TITLE/mcdbg.txt" >/dev/null; rm -f "$tmp"; echo "sent" ;;
  log)
    "$ADB" pull "$APP/log/eden_log.txt" "$LOGCOPY" >/dev/null; tail -n "${2:-40}" "$LOGCOPY" | cut -c1-220 ;;
  mcdbg)
    "$ADB" pull "$APP/log/eden_log.txt" "$LOGCOPY" >/dev/null; grep -E 'mcdbg|DSMod module|inventory ' "$LOGCOPY" | tail -60 | sed -E 's/^.*(mcdbg: |DSMod )/\1/' | cut -c1-220 ;;
  *) sed -n 2,6p "$0" ;;
esac
