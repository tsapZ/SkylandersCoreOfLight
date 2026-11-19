#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
version="$(sed -nE 's/.*set\(COREOFLIGHT_VERSION "([^"]+)"\).*/\1/p' "$repo/CMakeLists.txt" | head -n1)"
if [ -z "$version" ]; then
  echo "COREOFLIGHT_VERSION not found in CMakeLists.txt" >&2
  exit 1
fi

bin="$repo/out/build/linux-amd64-release/coreoflight"
if [ ! -x "$bin" ]; then
  echo "$bin is missing: build the linux-amd64-release preset first" >&2
  exit 1
fi

staging="$repo/installer/staging"
pkg_dir="coreoflight-linux-amd64-release"
stage="$staging/$pkg_dir"
rm -rf "$staging"
mkdir -p "$stage"

cp "$bin" "$stage/coreoflight"
chmod 0755 "$stage/coreoflight"
if command -v strip >/dev/null 2>&1; then
  strip --strip-unneeded "$stage/coreoflight"
fi

# SDL3 is not in Ubuntu 24.04 or Debian 12, so the launcher carries the one it was linked
# against: the binary's RPATH is $ORIGIN (CMakeLists.txt), which finds it next to itself.
sdl3="$(ldd "$bin" 2>/dev/null | awk '/[[:space:]]libSDL3\.so\.0 => / {print $3; exit}')"
if [ -z "$sdl3" ] || [ ! -f "$sdl3" ]; then
  echo "libSDL3.so.0 not found: the launcher links it, and Ubuntu 24.04 has no SDL3 package." >&2
  echo "Build and install SDL 3.4.20 first (or point CMAKE_PREFIX_PATH at it)." >&2
  exit 1
fi
cp -L "$sdl3" "$stage/libSDL3.so.0"
chmod 0644 "$stage/libSDL3.so.0"

# SDL3 installs its notice at share/licenses/SDL3. Walk up from the library
# so both lib/ and lib/<multiarch-triplet>/ find the installation prefix.
sdl_dir="$(dirname "$(realpath "$sdl3")")"
while [ "$sdl_dir" != / ]; do
  notice="$sdl_dir/share/licenses/SDL3/LICENSE.txt"
  if [ -f "$notice" ]; then
    cp "$notice" "$stage/LICENSE-SDL3.txt"
    break
  fi
  sdl_dir="$(dirname "$sdl_dir")"
done
if [ ! -f "$stage/LICENSE-SDL3.txt" ]; then
  for notice in /usr/share/doc/libsdl3-0/copyright /usr/share/doc/libsdl3-dev/copyright; do
    if [ -f "$notice" ]; then
      cp "$notice" "$stage/LICENSE-SDL3.txt"
      break
    fi
  done
fi
if [ ! -f "$stage/LICENSE-SDL3.txt" ]; then
  echo "SDL3 license not found for $sdl3: install SDL3 with its license before packaging" >&2
  exit 1
fi

cp -r "$repo/fonts" "$stage/fonts"
cp "$repo/LICENSE" "$stage/LICENSE"
rules="$repo/thirdparty/skylanders-portal/installer/70-portal-of-power.rules"
if [ -f "$rules" ]; then
  cp "$rules" "$stage/70-portal-of-power.rules"
fi

# The staged copy has to find its own SDL3 (the build machine's would be gone on the target).
resolved="$(ldd "$stage/coreoflight" 2>/dev/null | awk '/[[:space:]]libSDL3\.so\.0 => / {print $3; exit}')"
case "$resolved" in
  "$stage"/*) ;;
  *)
    echo "the staged launcher resolves SDL3 as ${resolved:-nothing}, not from $stage" >&2
    exit 1
    ;;
esac

out="$repo/installer/Output"
mkdir -p "$out"
tarball="$out/CoreOfLight-linux-amd64.tar.gz"
tar -C "$staging" -czf "$tarball" "$pkg_dir"
ls -lh "$tarball"
