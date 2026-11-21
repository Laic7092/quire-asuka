#!/usr/bin/env bash
#
# Builds the book engine (ebook-engine's C ABI, crates/ebook-abi) as a static
# library for one target, into lib/<target>/libebook_abi.a, where the app's
# asuka.toml finds it ([c.prebuilt]).
#
#   .github/scripts/build-engine.sh ios-arm64 ../ebook-engine
#
# The engine is Rust with no C dependencies, so cargo builds it with rustup's
# own target support: no NDK for Android beyond the linker, no Xcode beyond the
# SDK it finds.

set -euo pipefail

target="${1:?target: macos-arm64, macos-x64, ios-arm64, ios-simulator-arm64, ios-simulator-x64, linux-x64, android-arm64}"
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

# The `native` profile: panic = unwind, so the ABI's catch_unwind works, and no
# LTO (the linker embedding it isn't LLVM's).
cargo build \
  --manifest-path "$source_dir/Cargo.toml" \
  -p ebook-abi \
  --profile native \
  --target "$triple"

library="$source_dir/target/$triple/native/libebook_abi.a"
if [ ! -f "$library" ]; then
  echo "build-engine: cargo wrote no $library" >&2
  exit 1
fi

mkdir -p "$app_dir/lib/$target"
cp "$library" "$app_dir/lib/$target/libebook_abi.a"
echo "build-engine: $target → lib/$target/libebook_abi.a ($(du -h "$app_dir/lib/$target/libebook_abi.a" | cut -f1))"
