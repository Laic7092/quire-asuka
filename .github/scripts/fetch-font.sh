#!/usr/bin/env bash
#
# Builds the WOFF2 the app embeds (fonts/NotoSerifSC-Regular.woff2) from the
# upstream OTF. The face is 8 MB and the same on every machine, so the
# repository doesn't carry it: this script makes it, and checks that what it
# made is the face the pages were set in.
#
#   .github/scripts/fetch-font.sh
#
# Noto Serif CJK lives at github.com/notofonts/noto-cjk; the OTF is pinned by
# tag and checked by hash, and woff2_compress is deterministic, so the WOFF2
# comes out byte for byte the font the app shipped with. Needs woff2_compress:
#
#   apt-get install woff2        # Debian, Ubuntu
#   brew install woff2           # macOS
#
# FONT_OTF overrides where the OTF comes from (a mirror, or path/file://), and
# WOFF2_COMPRESS where woff2_compress is.

set -euo pipefail

otf="${FONT_OTF:-https://cdn.jsdelivr.net/gh/notofonts/noto-cjk@Serif2.003/Serif/SubsetOTF/SC/NotoSerifSC-Regular.otf}"
otf_sha256="e8f396decc1f0963a016a989c3d8852e863d1350996f573860a80767c83a1cd3"
woff2_sha256="62f910a39108a552c0f88b14f7f14e58bbcad35b1d41e524b73fcde783c7dd67"

app_dir="$(cd "$(dirname "$0")/../.." && pwd)"
out="$app_dir/fonts/NotoSerifSC-Regular.woff2"

# shasum is macOS's; sha256sum is everyone else's.
sum() {
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$1" | awk '{ print $1 }'
  else
    shasum -a 256 "$1" | awk '{ print $1 }'
  fi
}

if [ -f "$out" ] && [ "$(sum "$out")" = "$woff2_sha256" ]; then
  echo "fetch-font: fonts/NotoSerifSC-Regular.woff2 is here ($(du -h "$out" | cut -f1))"
  exit 0
fi

compress="${WOFF2_COMPRESS:-$(command -v woff2_compress || true)}"
if [ -z "$compress" ] || [ ! -x "$compress" ]; then
  echo "fetch-font: no woff2_compress (apt-get install woff2, or brew install woff2)" >&2
  exit 1
fi

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

fetch() {
  local url="$1" dest="$2"
  if [ -e "$url" ]; then
    cp "$url" "$dest"
  else
    curl -fsSL --retry 3 --retry-delay 2 -o "$dest" "$url"
  fi
}

echo "fetch-font: the OTF ($(printf '%s' "$otf" | sed 's/.*@//'))"
fetch "$otf" "$tmp/NotoSerifSC-Regular.otf"
got="$(sum "$tmp/NotoSerifSC-Regular.otf")"
if [ "$got" != "$otf_sha256" ]; then
  echo "fetch-font: the OTF is not the one pinned ($got, not $otf_sha256)" >&2
  exit 1
fi

echo "fetch-font: woff2_compress"
"$compress" "$tmp/NotoSerifSC-Regular.otf" >/dev/null
got="$(sum "$tmp/NotoSerifSC-Regular.woff2")"
if [ "$got" != "$woff2_sha256" ]; then
  echo "fetch-font: the WOFF2 is not the face the app ships ($got, not $woff2_sha256)" >&2
  exit 1
fi

mkdir -p "$app_dir/fonts"
install -m 644 "$tmp/NotoSerifSC-Regular.woff2" "$out"
echo "fetch-font: fonts/NotoSerifSC-Regular.woff2 ($(du -h "$out" | cut -f1))"
