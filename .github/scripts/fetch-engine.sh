#!/usr/bin/env bash
#
# Downloads the book engine's prebuilt static library (ebook-engine's
# releases) into lib/<target>/, where asuka.toml's [c.prebuilt] finds it.
#
#   .github/scripts/fetch-engine.sh ios-arm64 linux-x64 macos-arm64
#
# ENGINE_VERSION is the release: `latest` (the default) follows the newest
# non-prerelease, or give a tag (v0.2.0) to pin one. ENGINE_BASE overrides
# where they come from (a mirror, or file:// for a directory of them).

set -euo pipefail

version="${ENGINE_VERSION:-latest}"
if [ "$version" = "latest" ]; then
  base="${ENGINE_BASE:-https://github.com/Laic7092/ebook-engine/releases/latest/download}"
else
  base="${ENGINE_BASE:-https://github.com/Laic7092/ebook-engine/releases/download/$version}"
fi
app_dir="$(cd "$(dirname "$0")/../.." && pwd)"

if [ "$#" -eq 0 ]; then
  set -- linux-x64
fi

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

fetch() {
  local url="$1" out="$2"
  if [ -e "$url" ]; then
    cp "$url" "$out"
  else
    curl -fsSL --retry 3 --retry-delay 2 -o "$out" "$url"
  fi
}

# The release's checksums, once: every target is checked against them.
sums="$tmp/SHA256SUMS"
if ! fetch "$base/SHA256SUMS" "$sums" 2>/dev/null; then
  echo "fetch-engine: no SHA256SUMS at $base (checking nothing)" >&2
  : > "$sums"
fi

for target in "$@"; do
  file="libebook_abi-$target.tar.gz"
  echo "fetch-engine: $target"
  fetch "$base/$file" "$tmp/$file"

  if [ -s "$sums" ]; then
    want="$(awk -v f="$file" '$2 == f { print $1 }' "$sums")"
    if [ -n "$want" ]; then
      got="$(sha256sum "$tmp/$file" | awk '{ print $1 }')"
      if [ "$want" != "$got" ]; then
        echo "fetch-engine: $file is not the one released ($got, not $want)" >&2
        exit 1
      fi
    fi
  fi

  mkdir -p "$app_dir/lib/$target"
  tar -xzf "$tmp/$file" -C "$app_dir/lib/$target"
  # Unix's is an archive of .o; Windows' is MSVC's, and ebook-engine names it
  # without the lib prefix.
  lib="libebook_abi.a"
  case "$target" in windows-*) lib="ebook_abi.lib" ;; esac
  if [ ! -f "$app_dir/lib/$target/$lib" ]; then
    echo "fetch-engine: $file has no $lib" >&2
    exit 1
  fi
  echo "fetch-engine: lib/$target/$lib ($(du -h "$app_dir/lib/$target/$lib" | cut -f1))"
done
