#!/bin/bash
# Installs the status mod into Persona 5 Royal (Steam, in DroidDeck on an Android handheld) over adb.
#   mod/install_device.sh "<game folder on the device>" [prebuilt steam_api64.dll from the release]
# The game's own steam_api64.dll is kept next to it as steam_api64_o.dll; the mod takes its name and forwards
# every call to it. Without a prebuilt DLL the loader is built here (needs x86_64-w64-mingw32-gcc).
# Undo: mod/uninstall_device.sh. adb uses ANDROID_SERIAL if set.
set -euo pipefail
GAME=${1:?game folder on the device}; DLL=${2:-}
HERE="$(cd "$(dirname "$0")" && pwd)"; TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
adb shell "test -f '$GAME/P5R.exe'" || { echo "No Persona 5 Royal in $GAME"; exit 1; }
if adb shell "test -f '$GAME/steam_api64_o.dll'"; then SRC="$GAME/steam_api64_o.dll"; else SRC="$GAME/steam_api64.dll"; fi
if [ -z "$DLL" ]; then
  adb pull -q "$SRC" "$TMP/steam_api64.dll" >/dev/null
  "$HERE/make_proxy.sh" "$TMP/steam_api64.dll" >/dev/null; DLL="$HERE/out/steam_api64.dll"
fi
adb shell "cd '$GAME' && [ -f steam_api64_o.dll ] || cp -p steam_api64.dll steam_api64_o.dll"
adb push -q "$DLL" "$GAME/steam_api64.dll" >/dev/null
adb shell "chmod a+rw '$GAME/steam_api64.dll' '$GAME/steam_api64_o.dll' 2>/dev/null" || true
echo "Status mod installed in $GAME (original kept as steam_api64_o.dll)"
