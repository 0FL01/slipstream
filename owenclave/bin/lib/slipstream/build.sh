#!/usr/bin/env bash
set -euo pipefail

# Effective Android patches are already vendored; see docs/source-provenance.md.
RUST_VERSION="1.97.1"
CARGO_NDK_VERSION="4.1.2"
NDK_VERSION="29.0.14206865"
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
SRC="${SLIPSTREAM_SRC:-$ROOT/../slipstream-rust}"
# Per-checkout output avoids the old cache's CMake source-directory binding.
BUILD="${SLIPSTREAM_BUILD:-$ROOT/../.cache/slipstream-android}"
OUT="$ROOT/app/src/main/jniLibs/arm64-v8a/libslipstream.so"
TC="${ANDROID_NDK_HOME:?ANDROID_NDK_HOME is required}/toolchains/llvm/prebuilt/linux-x86_64/bin"

test -f "$SRC/Cargo.toml"
test -f "$SRC/crates/slipstream-client/src/flow_relay.rs"
test -f "$SRC/vendor/picoquic/CMakeLists.txt"
command -v cargo >/dev/null
command -v cmake >/dev/null
command -v perl >/dev/null
rustc --version | grep -q "^rustc $RUST_VERSION "
cargo ndk --version | grep -q "cargo-ndk $CARGO_NDK_VERSION"
grep -q "Pkg.Revision = $NDK_VERSION" "$ANDROID_NDK_HOME/source.properties"
perl -MFindBin -MFile::Compare -e 1

mkdir -p "$BUILD" "$(dirname "$OUT")"
# Never clone, reset or patch SRC: it is editable monorepo source.

export ANDROID_ABI=arm64-v8a
export ANDROID_PLATFORM=android-21
export CARGO_TARGET_DIR="$BUILD/target"
export PICOQUIC_BUILD_DIR="$BUILD/picoquic"
export PKG_CONFIG_LIBDIR="$BUILD/pkgconfig"
export RUST_ANDROID_GRADLE_CC="$TC/aarch64-linux-android21-clang"
export RUST_ANDROID_GRADLE_AR="$TC/llvm-ar"
mkdir -p "$PKG_CONFIG_LIBDIR"

(cd "$SRC" && cargo ndk -t arm64-v8a --platform 21 build \
  --release --locked -p slipstream-client \
  --features openssl-vendored,picoquic-minimal-build)
install -m 0755 "$CARGO_TARGET_DIR/aarch64-linux-android/release/slipstream-client" "$OUT"
sha256sum "$OUT"
