#!/usr/bin/env bash
#
# Builds the book engine (ebook-engine's C ABI, crates/ebook-abi) as a static
# library for one target, into lib/<target>/libebook_abi.a, where the app's
# asuka.toml finds it ([c.prebuilt]).
#
#   .github/scripts/build-engine.sh ios-arm64 ../ebook-engine
#   .github/scripts/build-engine.sh web ../ebook-engine
#   .github/scripts/build-engine.sh windows-x64 ../ebook-engine
#
# The engine is Rust with no C dependencies, so cargo builds it with rustup's
# own target support: no NDK for Android beyond the linker, no Xcode beyond the
# SDK it finds.

set -euo pipefail

target="${1:?target: macos-arm64, macos-x64, ios-arm64, ios-simulator-arm64, ios-simulator-x64, linux-x64, android-arm64, windows-x64, web}"
source_dir="${2:-engine}"
app_dir="$(cd "$(dirname "$0")/../.." && pwd)"

case "$target" in
  macos-arm64) triple=aarch64-apple-darwin ;;
  macos-x64) triple=x86_64-apple-darwin ;;
  ios-arm64) triple=aarch64-apple-ios ;;
  ios-simulator-arm64) triple=aarch64-apple-ios-sim ;;
  ios-simulator-x64) triple=x86_64-apple-ios ;;
  linux-x64) triple=x86_64-unknown-linux-gnu ;;
  linux-arm64) triple=aarch64-unknown-linux-gnu ;;
  android-arm64) triple=aarch64-linux-android ;;
  android-x64) triple=x86_64-linux-android ;;
  # The web's: asuka's wasm build links it with wasi-libc, and c/wasm-pthread.c
  # supplies the pthread calls wasm's std names that libc doesn't have.
  web) triple=wasm32-wasip1 ;;
  # The Windows app links with mingw-w64 (or Zig's cc, which is the same CRT),
  # so it's this triple, not MSVC's: mixing the two CRTs is a pile of
  # duplicate symbols at the link.
  windows-x64) triple=x86_64-pc-windows-gnu ;;
  *)
    echo "build-engine: unknown target $target" >&2
    exit 1
    ;;
esac

if [ ! -f "$source_dir/Cargo.toml" ]; then
  echo "build-engine: no engine at $source_dir (check it out beside the app)" >&2
  exit 1
fi

rustup target add "$triple" >/dev/null 2>&1 || true

# A static library still needs an archiver: mingw's for the Windows target.
case "$target" in
  windows-*) export "CARGO_TARGET_$(echo "$triple" | tr 'a-z-' 'A-Z_')_AR=x86_64-w64-mingw32-ar" ;;
esac

# The `native` profile: panic = unwind, so the ABI's catch_unwind works, and no
# LTO (the linker embedding it isn't LLVM's). wasm can't unwind, so the web's
# library is the release profile: a panic there ends the page, as it would
# natively.
profile=native
case "$target" in web) profile=release ;; esac

cargo build \
  --manifest-path "$source_dir/Cargo.toml" \
  -p ebook-abi \
  --profile "$profile" \
  --target "$triple"

library="$source_dir/target/$triple/$profile/libebook_abi.a"
if [ ! -f "$library" ]; then
  echo "build-engine: cargo wrote no $library" >&2
  exit 1
fi

mkdir -p "$app_dir/lib/$target"
cp "$library" "$app_dir/lib/$target/libebook_abi.a"
echo "build-engine: $target → lib/$target/libebook_abi.a ($(du -h "$app_dir/lib/$target/libebook_abi.a" | cut -f1))"
