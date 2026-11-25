#!/usr/bin/env bash
#
# Downloads legado-engine's prebuilt static library (the online book sources'
# rules and JavaScript) into lib/<target>/, where asuka.toml's [c.prebuilt]
# finds it.
#
#   .github/scripts/fetch-legado.sh linux-x64 ios-arm64 macos-arm64
#
# LEGADO_VERSION is the release: `latest` (the default) follows the newest
# non-prerelease, or give a tag (v0.2.0) to pin one. LEGADO_BASE overrides
# where they come from (a mirror, or file:// for a directory of them).

set -euo pipefail

version="${LEGADO_VERSION:-latest}"
if [ "$version" = "latest" ]; then
  base="${LEGADO_BASE:-https://github.com/Laic7092/legado-engine/releases/latest/download}"
else
  base="${LEGADO_BASE:-https://github.com/Laic7092/legado-engine/releases/download/$version}"
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

for target in "$@"; do
  file="legado-engine-$target.tar.gz"
  echo "fetch-legado: $target"
  fetch "$base/$file" "$tmp/$file"

  mkdir -p "$app_dir/lib/$target"
  # The archive is one directory named after the target: its library, and the
  # header and licenses beside it. Only the library is the app's (the header
  # is c/legado.h, which this repository carries).
  tar -xzf "$tmp/$file" -C "$app_dir/lib/$target" --strip-components=1 "$target/liblegado_engine.a"

  if [ ! -f "$app_dir/lib/$target/liblegado_engine.a" ]; then
    echo "fetch-legado: $file has no liblegado_engine.a" >&2
    exit 1
  fi
  echo "fetch-legado: lib/$target/liblegado_engine.a ($(du -h "$app_dir/lib/$target/liblegado_engine.a" | cut -f1))"
done
