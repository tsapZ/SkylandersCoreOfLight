#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
version="$(sed -nE 's/.*set\(COREOFLIGHT_VERSION "([^"]+)"\).*/\1/p' "$repo/CMakeLists.txt" | head -n1)"
if [ -z "$version" ]; then
  echo "COREOFLIGHT_VERSION not found in CMakeLists.txt" >&2
  exit 1
fi

# COREOFLIGHT_ARCH=arm64 (or amd64) packages a launcher cross-built for that architecture, e.g. ARM64 from an x86-64
# machine with qemu-user (the library check runs the target's own loader); default: this machine.
host_arch="$(uname -m)"
case "${COREOFLIGHT_ARCH:-$host_arch}" in
  x86_64|amd64) arch="amd64" ;;
  aarch64|arm64) arch="arm64" ;;
  *)
    echo "Unsupported machine architecture: $(uname -m) (expected x86_64 or aarch64)" >&2
    exit 1
    ;;
esac

preset="linux-$arch-release"
bin="$repo/out/build/$preset/coreoflight"
if [ ! -x "$bin" ]; then
  echo "$bin is missing: build the $preset preset first" >&2
  exit 1
fi
if command -v readelf >/dev/null 2>&1; then
  machine="$(readelf -h "$bin" | awk -F: '/Machine:/ {gsub(/^[ \t]+/, "", $2); print $2; exit}')"
  case "$arch" in
    amd64) expected="X86-64" ;;
    arm64) expected="AArch64" ;;
  esac
  case "$machine" in
    *"$expected"*) ;;
    *)
      echo "$bin is $machine but the host is $arch: build the $preset preset on this machine" >&2
      exit 1
      ;;
  esac
fi

staging="$repo/installer/staging"
pkg_dir="coreoflight-linux-$arch-release"
stage="$staging/$pkg_dir"
rm -rf "$staging"
mkdir -p "$stage"

# The libraries a binary resolves (ldd), through the target's loader under qemu when it is for another machine.
list_deps() {
  case "$arch:$host_arch" in
    arm64:aarch64|arm64:arm64|amd64:x86_64|amd64:amd64) ldd "$1" 2>/dev/null ;;
    arm64:*) qemu-aarch64-static -L / /lib/ld-linux-aarch64.so.1 --list "$1" 2>/dev/null ;;
    amd64:*) qemu-x86_64-static -L / /lib64/ld-linux-x86-64.so.2 --list "$1" 2>/dev/null ;;
  esac
}

cp "$bin" "$stage/coreoflight"
chmod 0755 "$stage/coreoflight"
if [ -n "${COREOFLIGHT_ARCH:-}" ] && command -v llvm-strip-19 >/dev/null 2>&1; then
  llvm-strip-19 --strip-unneeded "$stage/coreoflight"  # (a cross build: GNU strip knows only the host's format)
elif command -v strip >/dev/null 2>&1; then
  strip --strip-unneeded "$stage/coreoflight"
fi

# SDL3 is not in Ubuntu 24.04 or Debian 12, and SteamOS doesn't package the 3.4 line, so the
# launcher carries the one it was linked against: the binary's RPATH is $ORIGIN (CMakeLists.txt),
# which finds it next to itself.
sdl3="$(list_deps "$bin" | awk '/[[:space:]]libSDL3\.so\.0 => / && !f {print $3; f = 1}')"
if [ -z "$sdl3" ] || [ ! -f "$sdl3" ]; then
  echo "libSDL3.so.0 not found: the launcher links it, and the base system may not package SDL3." >&2
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
# The notices of the code built into the launcher (Dear ImGui MIT, zstd BSD, ZArchive MIT-0; #166).
mkdir -p "$stage/licenses"
cp "$repo/thirdparty/imgui/LICENSE.txt" "$stage/licenses/imgui.txt"
cp "$repo/thirdparty/zstd/LICENSE" "$stage/licenses/zstd.txt"
cp "$repo/thirdparty/zarchive/LICENSE" "$stage/licenses/zarchive.txt"
rules="$repo/thirdparty/skylanders-portal/installer/70-portal-of-power.rules"
if [ -f "$rules" ]; then
  cp "$rules" "$stage/70-portal-of-power.rules"
fi

# The staged copy has to find its own SDL3 (the build machine's would be gone on the target).
resolved="$(list_deps "$stage/coreoflight" | awk '/[[:space:]]libSDL3\.so\.0 => / && !f {print $3; f = 1}')"
case "$resolved" in
  "$stage"/*) ;;
  *)
    echo "the staged launcher resolves SDL3 as ${resolved:-nothing}, not from $stage" >&2
    exit 1
    ;;
esac

out="$repo/installer/Output"
mkdir -p "$out"
tarball="$out/CoreOfLight-linux-$arch.tar.gz"
tar -C "$staging" -czf "$tarball" "$pkg_dir"
ls -lh "$tarball"
