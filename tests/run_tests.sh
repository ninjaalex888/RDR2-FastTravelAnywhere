#!/usr/bin/env bash
# Builds the fake Script Hook + test host and runs the end-to-end test under Wine.
# Requires: build/FastTravelAnywhere.asi (run ./build.sh first), zig (pip install ziglang), wine.
set -euo pipefail
cd "$(dirname "$0")/.."
ZIG="${ZIG:-python3 -m ziglang}"
WINE="${WINE:-$(command -v wine64 || command -v wine || echo /usr/lib/wine/wine64)}"
T=build/test
rm -rf "$T" && mkdir -p "$T"
$ZIG c++ -target x86_64-windows-gnu -O2 -std=c++17 -shared -o "$T/ScriptHookRDR2.dll" tests/fake_scripthook.cpp
$ZIG c++ -target x86_64-windows-gnu -O2 -std=c++17 -o "$T/test_host.exe" tests/test_host.cpp
rm -f "$T"/*.lib "$T"/*.pdb
cp build/FastTravelAnywhere.asi "$T/"
cd "$T"
WINEPREFIX="${WINEPREFIX:-$HOME/.wine-fasttravelanywhere}" WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml=" \
  "$WINE" test_host.exe
