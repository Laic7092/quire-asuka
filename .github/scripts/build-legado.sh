#!/usr/bin/env bash
#
# Builds legado-engine (the online book sources' rules and JavaScript) as a
# static library for one target, into lib/<target>/liblegado_engine.a, where
# the app's asuka.toml finds it ([c.prebuilt]).
#
#   .github/scripts/build-legado.sh linux-x64 ../legado-engine
#
# The engine is Rust; rquickjs is built by the same cargo run. iOS needs Xcode
# (bindgen), Android isn't a target legado-engine builds here, and Windows
# wants the MSVC library the release carries (asuka's Windows build links
# mingw, which this app doesn't do yet — see README.md).

set -euo pipefail

target="${1:?target: macos-arm64, macos-x64, ios-arm64, linux-x64}"
source_dir="${2:-legado-engine}"
app_dir="$(cd "$(dirname "$0")/../.." && pwd)"

case "$target" in
  macos-arm64) triple=aarch64-apple-darwin ;;
  macos-x64) triple=x86_64-apple-darwin ;;
  ios-arm64) triple=aarch64-apple-ios ;;
  linux-x64) triple=x86_64-unknown-linux-gnu ;;
  *)
    echo "build-legado: unknown target $target" >&2
    exit 1
    ;;
esac

if [ ! -f "$source_dir/Cargo.toml" ]; then
  echo "build-legado: no legado-engine at $source_dir (check it out beside the app)" >&2
  exit 1
fi

rustup target add "$triple" >/dev/null 2>&1 || true

# iOS: rquickjs-sys has no pregenerated bindings, so its build.rs runs bindgen.
if [ "$triple" = "aarch64-apple-ios" ]; then
  export LIBCLANG_PATH="${LIBCLANG_PATH:-$(xcode-select -p)/Toolchains/XcodeDefault.xctoolchain/usr/lib}"
fi

cargo build \
  --locked \
  --manifest-path "$source_dir/Cargo.toml" \
  --release \
  --lib \
  --target "$triple"

library="$source_dir/target/$triple/release/liblegado_engine.a"
if [ ! -f "$library" ]; then
  echo "build-legado: cargo wrote no $library" >&2
  exit 1
fi

mkdir -p "$app_dir/lib/$target"
cp "$library" "$app_dir/lib/$target/liblegado_engine.a"
echo "build-legado: $target → lib/$target/liblegado_engine.a ($(du -h "$app_dir/lib/$target/liblegado_engine.a" | cut -f1))"
