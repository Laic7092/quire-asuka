# Fonts

`NotoSerifSC-Regular.woff2` is Noto Serif SC Regular, under the SIL Open Font
License in `OFL.txt`. It is the font the app embeds for every book, so the
pages are set the same way on every platform.

The WOFF2 is not in the repository: it is 8 MB, and it is a file that can be
made again. `.github/scripts/fetch-font.sh` downloads the upstream OTF (pinned
by tag, checked by hash), compresses it with `woff2_compress`, and checks that
the result is byte for byte the face the app shipped with:

```sh
.github/scripts/fetch-font.sh
```

It needs `woff2_compress`: `apt-get install woff2` on Linux, `brew install
woff2` on macOS. `asuka check`, `asuka test` and `asuka dev` embed the font, so
a fresh clone fetches it once before the first check.

It is WOFF2 because the engine's `FontBook` reads WOFF2 directly and the file
is about a third smaller than the OTF; the OTF is downloaded to build it, not
kept.
