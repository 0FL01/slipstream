# Slipstream mobile

Single-root source snapshot for Owenclave's Android DNS carrier and the separate C experiment. All three trees and their distinct dependencies are ordinary vendored directories; upstream histories remain in their original repositories.

## Start here
- [Architecture](docs/architecture.md): Android Rust carrier, C experiment, sole FlowRelay owner.
- [Source provenance](docs/source-provenance.md): exact upstream/effective-source inventory and omitted secrets.
- [Operations boundary](docs/operations-boundary.md): live authority, DE MCP prerequisites and shared RA74 backend.
- [Migration contract and verification](docs/goals/2026-11-14-source-migration.md).
- [Owenclave](owenclave/README.md), [DNS carrier contract](owenclave/docs/dns-tunnel.md), [Android routing](owenclave/docs/android-network-routing.md).
- [C benchmark](slipstream-c/bench/README.md), [C source](slipstream-c/README.md), [Rust source](slipstream-rust/README.md).
- Historical [RU–DE multipath](docs/goals/2026-10-30-ru-de-slipstream-multipath.md) and [five-DNS](docs/goals/2026-10-30-ru-de-slipstream-five-dns.md) results; aggregate evidence under `docs/evidence/`.

## Local checks
```sh
python3 -B -m unittest discover -s slipstream-c/bench -p 'test*.py' -v
meson setup slipstream-c/build-candidate slipstream-c
meson compile -C slipstream-c/build-candidate
meson test -C slipstream-c/build-candidate --print-errorlogs
(cd owenclave/bin/lib/slipstream/flowd && go test ./...)
```

Android: enter `owenclave/` and use `nix develop` where available (JDK21, SDK37, NDK29). Run `./run lib core`, `./bin/lib/slipstream/build.sh`, `./gradlew :app:downloadAssets`, then `./gradlew :app:compileOssReleaseKotlin` / `:app:assembleOssRelease`. Rust1.97.1, cargo-ndk4.1.2 and NDK29.0.14206865 remain pinned. The Slipstream script builds editable sibling `slipstream-rust/`; no source clone/reset/patch step runs.

Signing is external: set `KEYSTORE_PATH`, `KEYSTORE_PASSWORD`, `KEY_ALIAS`, `KEY_PASSWORD` privately. Without `KEYSTORE_PATH`, release is unsigned. No production key is generated or imported.

Nested `.github/workflows/` are preserved source history and do not execute from this root. This migration enables no root CI, releases, deployment or push workflow.
