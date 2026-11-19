#!/usr/bin/env bash
# The launcher as an AppImage: the tarball's files in an AppDir, packed with appimagetool.
#
#   bash tools/package_linux.sh        # the files to put into the AppDir
#   bash tools/package_appimage.sh     # -> installer/Output/CoreOfLight-<version>-x86_64.AppImage
#
# appimagetool and its runtime are downloaded once into $SKYLANDERS_WORK/tools (default
# ~/SkylandersDev/tools) and reused; APPIMAGETOOL and APPIMAGE_RUNTIME point at copies of your own.
# The AppImage runs like the tarball: the same fonts, the same $ORIGIN SDL3, the user's own games.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
version="$(sed -nE 's/.*set\(COREOFLIGHT_VERSION "([^"]+)"\).*/\1/p' "$repo/CMakeLists.txt" | head -n1)"
if [ -z "$version" ]; then
  echo "COREOFLIGHT_VERSION not found in CMakeLists.txt" >&2
  exit 1
fi

stage="$repo/installer/staging/coreoflight-linux-amd64-release"
if [ ! -x "$stage/coreoflight" ]; then
  echo "$stage/coreoflight is missing: run bash tools/package_linux.sh first" >&2
  exit 1
fi
if [ ! -f "$stage/libSDL3.so.0" ]; then
  echo "$stage/libSDL3.so.0 is missing: run bash tools/package_linux.sh first" >&2
  exit 1
fi

appdir="$repo/installer/staging/CoreOfLight.AppDir"
rm -rf "$appdir"
mkdir -p "$appdir/usr/bin" "$appdir/usr/share/applications" \
         "$appdir/usr/share/icons/hicolor/256x256/apps" "$appdir/usr/share/doc/coreoflight"

cp "$stage/coreoflight" "$appdir/usr/bin/coreoflight"
cp "$stage/libSDL3.so.0" "$appdir/usr/bin/libSDL3.so.0"
cp -r "$stage/fonts" "$appdir/usr/bin/fonts"
cp "$stage/LICENSE" "$appdir/usr/bin/LICENSE"
if [ -f "$stage/LICENSE-SDL3.txt" ]; then
  cp "$stage/LICENSE-SDL3.txt" "$appdir/usr/bin/LICENSE-SDL3.txt"
fi
if [ -f "$stage/70-portal-of-power.rules" ]; then
  cp "$stage/70-portal-of-power.rules" "$appdir/usr/share/doc/coreoflight/70-portal-of-power.rules"
fi

# The icon: the 256 px PNG inside the repository's Windows .ico, so there is no second copy in git.
python3 - "$repo/src/coreoflight.ico" "$appdir/coreoflight.png" <<'PY'
import struct
import sys

src, dst = sys.argv[1], sys.argv[2]
data = open(src, "rb").read()
count = struct.unpack_from("<H", data, 4)[0]
for i in range(count):
    width, _, _, _, _, _, size, offset = struct.unpack_from("<BBBBHHII", data, 6 + i * 16)
    if (width or 256) == 256 and data[offset:offset + 4] == b"\x89PNG":
        open(dst, "wb").write(data[offset:offset + size])
        break
else:
    raise SystemExit(f"{src}: no 256x256 PNG icon")
PY
cp "$appdir/coreoflight.png" "$appdir/usr/share/icons/hicolor/256x256/apps/coreoflight.png"

# AppRun: the root of the mount, the binary and its $ORIGIN libraries in usr/bin.
cat > "$appdir/AppRun" <<'EOF'
#!/bin/sh
here="$(dirname "$(readlink -f "$0")")"
exec "$here/usr/bin/coreoflight" "$@"
EOF
chmod 0755 "$appdir/AppRun"

cat > "$appdir/coreoflight.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Core of Light
Comment=The launcher for the six recompiled Skylanders games
Exec=coreoflight
Icon=coreoflight
Terminal=false
Categories=Game;
X-AppImage-Version=$version
EOF
cp "$appdir/coreoflight.desktop" "$appdir/usr/share/applications/coreoflight.desktop"

cache="${SKYLANDERS_WORK:-$HOME/SkylandersDev}/tools"
mkdir -p "$cache"

tool="${APPIMAGETOOL:-}"
if [ -z "$tool" ] && command -v appimagetool >/dev/null 2>&1; then
  tool="$(command -v appimagetool)"
fi
if [ -z "$tool" ] && [ -x "$cache/appimagetool-x86_64.AppImage" ]; then
  tool="$cache/appimagetool-x86_64.AppImage"
fi
if [ -z "$tool" ]; then
  command -v curl >/dev/null 2>&1 || { echo "curl is needed to download appimagetool once" >&2; exit 1; }
  echo "downloading appimagetool into $cache (once) ..."
  curl -fL --retry 3 --connect-timeout 20 -o "$cache/appimagetool-x86_64.AppImage" \
    https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage
  chmod 0755 "$cache/appimagetool-x86_64.AppImage"
  tool="$cache/appimagetool-x86_64.AppImage"
fi

runtime="${APPIMAGE_RUNTIME:-}"
if [ -z "$runtime" ]; then
  runtime="$cache/runtime-x86_64"
  if [ ! -f "$runtime" ]; then
    command -v curl >/dev/null 2>&1 || { echo "curl is needed to download the AppImage runtime once" >&2; exit 1; }
    echo "downloading the AppImage runtime into $cache (once) ..."
    curl -fL --retry 3 --connect-timeout 20 -o "$runtime" \
      https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-x86_64
  fi
fi
if [ ! -f "$tool" ] || [ ! -f "$runtime" ]; then
  echo "appimagetool ($tool) or the runtime ($runtime) is missing" >&2
  exit 1
fi
sha256sum "$tool" "$runtime"

out="$repo/installer/Output"
mkdir -p "$out"
appimage="$out/CoreOfLight-$version-x86_64.AppImage"
ARCH=x86_64 VERSION="$version" APPIMAGE_EXTRACT_AND_RUN=1 \
  "$tool" --no-appstream --runtime-file "$runtime" "$appdir" "$appimage"
chmod 0755 "$appimage"
ls -lh "$appimage"
echo "Ubuntu 24.04 users need libfuse2t64 (or --appimage-extract-and-run) to start an AppImage."
