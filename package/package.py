#!/usr/bin/env python3
"""Packages dist/FastTravelAnywhere-<version>.zip from an existing build/ (run ./build.sh first).
Top level: the two game files and the player README. Full source in source/."""
import os, re, shutil, zipfile
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
ver = re.search(r'#define FTA_VERSION "([^"]+)"', open("src/FastTravelAnywhere.cpp").read()).group(1)
name = f"FastTravelAnywhere-{ver}"
stage = f"dist/{name}"
shutil.rmtree("dist", ignore_errors=True)
os.makedirs(stage)

def crlf(src, dst):
    t = open(src, encoding="utf-8-sig").read().replace("\r\n", "\n").replace("\n", "\r\n")
    open(dst, "w", newline="", encoding="utf-8").write(t)

shutil.copy("build/FastTravelAnywhere.asi", stage)
shutil.copy("build/FastTravelAnywhere.ini", stage)
crlf("package/README.txt", f"{stage}/README.txt")

src = f"{stage}/source"
os.makedirs(src)
for d in ["src", "tests", "package"]:
    shutil.copytree(d, f"{src}/{d}", ignore=shutil.ignore_patterns("__pycache__"))
for f in ["README.md", "build.sh", ".gitignore"]:
    shutil.copy(f, src)
crlf("build_msvc.bat", f"{src}/build_msvc.bat")

zpath = f"dist/{name}.zip"
with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as z:
    for base, dirs, files in os.walk(stage):
        dirs.sort()
        for fn in sorted(files):
            full = os.path.join(base, fn)
            zi = zipfile.ZipInfo.from_file(full, os.path.relpath(full, "dist"))
            if fn.endswith(".sh"):
                zi.external_attr = 0o755 << 16
            with open(full, "rb") as fh:
                z.writestr(zi, fh.read(), zipfile.ZIP_DEFLATED)
shutil.rmtree(stage)
print(f"packaged {zpath} ({os.path.getsize(zpath)} bytes)")
