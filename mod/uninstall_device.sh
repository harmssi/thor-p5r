#!/bin/bash
# Removes the status mod again: puts the game's own steam_api64.dll back.
set -euo pipefail
GAME=${1:?game folder on the device}
adb shell "test -f '$GAME/steam_api64_o.dll'" || { echo "no steam_api64_o.dll in $GAME: the mod is not installed"; exit 1; }
adb shell "cd '$GAME' && mv steam_api64_o.dll steam_api64.dll && rm -f p5deck.log"
echo "Status mod removed (original steam_api64.dll restored)"
