# Fonts

`NotoSerifSC-Regular.woff2` is Noto Serif SC Regular, under the SIL Open Font
License in `OFL.txt`. It is the font the app embeds for every book, so the
pages are set the same way on every platform.

It is WOFF2 because the engine's `FontBook` reads WOFF2 directly and the
file is about a third smaller than the OTF. To rebuild it from the upstream
OTF:

```sh
woff2_compress NotoSerifSC-Regular.otf
```
