#!/bin/sh
# Unpack dist/0100D71004694000.dsmod.zip into the desktop Eden data folder (load/<title>/Minecraft),
# so eden-cli picks it up. Run scripts/build.sh first.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
[ -f "$ROOT/local.env" ] && . "$ROOT/local.env"
ZIP="$ROOT/dist/0100D71004694000.dsmod.zip"
DST="${EDEN_DATA:-$HOME/.local/share/eden}/load/0100D71004694000/Minecraft"
[ -f "$ZIP" ] || { echo "no $ZIP; run scripts/build.sh"; exit 1; }
rm -rf "$DST"; mkdir -p "$DST"
python3 -c "import sys,zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])" "$ZIP" "$DST"
echo "installed $ZIP -> $DST"
