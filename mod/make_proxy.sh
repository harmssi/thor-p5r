#!/bin/bash
# Builds the loader: a steam_api64.dll that forwards every export to the game's own DLL (renamed steam_api64_o.dll)
# and carries the status mod. The export NAMES are read from the player's own steam_api64.dll; nothing of it is copied.
#   mod/make_proxy.sh <the game's steam_api64.dll>   ->  mod/out/steam_api64.dll
set -euo pipefail
cd "$(dirname "$0")"; SRC=${1:?path to the game\'s own steam_api64.dll}; mkdir -p out
{ echo "LIBRARY steam_api64"; echo "EXPORTS"
  x86_64-w64-mingw32-objdump -p "$SRC" | awk '/\[Ordinal\/Name Pointer\] Table/{f=1;next} f&&NF<2{f=0} f{print $NF}' \
    | while read -r n; do echo "  $n = steam_api64_o.$n"; done; } > out/proxy.def
n=$(grep -c " = steam_api64_o\." out/proxy.def); [ "$n" -gt 50 ] || { echo "only $n exports found in $SRC"; exit 1; }
x86_64-w64-mingw32-gcc -O2 -Wall -Wextra -shared -static-libgcc -o out/steam_api64.dll \
  p5main.c p5core.c p5http.c p5state.c p5debug.c p5flow.c out/proxy.def ${P5_EXTRA:-} -lws2_32
echo "$n exports forwarded"; ls -la out/steam_api64.dll
