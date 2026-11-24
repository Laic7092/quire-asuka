#!/usr/bin/env bash
#
# Fetches the font the app embeds (fonts/NotoSerifSC-Regular.woff). The face
# is 8 MB and the same on every machine, so the repository doesn't carry it:
# this script downloads it, and checks that what it got is the face the pages
# were set in.
#
#   .github/scripts/fetch-font.sh
#
# Noto Serif SC 2.003, SIL OFL (fonts/OFL.txt). @openfonts republishes Google
# Fonts' build as one file per weight, and jsDelivr serves it by version; the
# hash below pins the exact bytes. The engine's FontBook reads WOFF as well as
# WOFF2, so there is nothing to install and nothing to convert.
#
# FONT_WOFF overrides where the font comes from (a mirror, or path/file://).

set -euo pipefail

woff="${FONT_WOFF:-https://cdn.jsdelivr.net/npm/@openfonts/noto-serif-sc_all@1.44.1/files/noto-serif-sc-all-400.woff}"
woff_sha256="e4c8addfaeedfca9cd3b805590336314786ebe9c356b2ff9a3963d53e53038ce"

app_dir="$(cd "$(dirname "$0")/../.." && pwd)"
out="$app_dir/fonts/NotoSerifSC-Regular.woff"

# shasum is macOS's; sha256sum is everyone else's.
sum() {
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$1" | awk '{ print $1 }'
  else
    shasum -a 256 "$1" | awk '{ print $1 }'
  fi
}

if [ -f "$out" ] && [ "$(sum "$out")" = "$woff_sha256" ]; then
  echo "fetch-font: fonts/NotoSerifSC-Regular.woff is here ($(du -h "$out" | cut -f1))"
  exit 0
fi

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

echo "fetch-font: the WOFF ($(printf '%s' "$woff" | sed 's/.*@//; s#/files/.*##'))"
if [ -e "$woff" ]; then
  cp "$woff" "$tmp/font.woff"
else
  curl -fsSL --retry 3 --retry-delay 2 -o "$tmp/font.woff" "$woff"
fi
got="$(sum "$tmp/font.woff")"
if [ "$got" != "$woff_sha256" ]; then
  echo "fetch-font: the WOFF is not the face the app ships ($got, not $woff_sha256)" >&2
  exit 1
fi

mkdir -p "$app_dir/fonts"
install -m 644 "$tmp/font.woff" "$out"
echo "fetch-font: fonts/NotoSerifSC-Regular.woff ($(du -h "$out" | cut -f1))"
