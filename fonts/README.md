# Fonts

`NotoSerifSC-Regular.woff` is Noto Serif SC Regular (version 2.003), under the
SIL Open Font License in `OFL.txt`. It is the font the app embeds for every
book, so the pages are set the same way on every platform.

The font is not in the repository: it is 8 MB, and it is a file that can be
downloaded again. `.github/scripts/fetch-font.sh` fetches it over jsDelivr and
checks it against the pinned hash:

```sh
.github/scripts/fetch-font.sh
```

`@openfonts/noto-serif-sc_all` republishes Google Fonts' build as one file per
weight; jsDelivr serves the pinned version, so the script downloads the exact
file the app embeds. There is no OTF to fetch and no `woff2_compress` to run,
and nothing to install.

It is WOFF because the engine's `FontBook` reads WOFF and WOFF2 directly, and
the file is about a third smaller than the OTF. Google's own CDN only serves
the full CJK face as a TTF (14.7 MB) or as 101 `unicode-range` slices; this one
file is the whole face (same version, same PostScript name, same 449 ranges as
the upstream OTF).

`asuka check`, `asuka test` and `asuka dev` embed the font, so a fresh clone
fetches it once before the first check.
