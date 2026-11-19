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

cp -r "$repo/fonts" "$stage/fonts"
cp "$repo/LICENSE" "$stage/LICENSE"
rules="$repo/thirdparty/skylanders-portal/installer/70-portal-of-power.rules"
if [ -f "$rules" ]; then
  cp "$rules" "$stage/70-portal-of-power.rules"
fi

out="$repo/installer/Output"
mkdir -p "$out"
tarball="$out/CoreOfLight-linux-amd64.tar.gz"
tar -C "$staging" -czf "$tarball" "$pkg_dir"
ls -lh "$tarball"
