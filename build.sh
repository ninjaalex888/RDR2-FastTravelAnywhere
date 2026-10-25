#!/usr/bin/env bash
# Cross-compiles FastTravelAnywhere.asi for 64-bit Windows using zig's bundled clang,
# then packages the release zip.
#   pip install ziglang      (or set ZIG=/path/to/zig)
#   ./build.sh               -> build/FastTravelAnywhere.asi, build/FastTravelAnywhere.ini,
#                               dist/FastTravelAnywhere-<version>.zip
set -euo pipefail
cd "$(dirname "$0")"

ZIG="${ZIG:-python3 -m ziglang}"
OUT=build
TARGET=x86_64-windows-gnu

rm -rf "$OUT" && mkdir -p "$OUT"

$ZIG c++ -target $TARGET -O2 -DNDEBUG -std=c++17 -fno-exceptions -fno-rtti -Wall -Wextra \
  -shared -s -o "$OUT/FastTravelAnywhere.dll" src/FastTravelAnywhere.cpp -luser32

mv "$OUT/FastTravelAnywhere.dll" "$OUT/FastTravelAnywhere.asi"
rm -f "$OUT"/*.lib "$OUT"/*.pdb

# Default settings file = the template embedded in the source (CRLF for Notepad).
python3 - "$OUT/FastTravelAnywhere.ini" <<'PY'
import re, sys
src = open("src/FastTravelAnywhere.cpp", encoding="utf-8").read()
body = src.split("static const char kDefaultIni[] =", 1)[1].split(";\n", 1)[0]
parts = re.findall(r'"((?:[^"\\]|\\.)*)"', body)
text = "".join(parts).encode().decode("unicode_escape")
open(sys.argv[1], "w", newline="").write(text)
PY

# Release zip (see package/package.py).
python3 package/package.py

ls -la "$OUT" dist
