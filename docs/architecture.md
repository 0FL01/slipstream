# Architecture and local build boundaries

## Android carrier
Owenclave owns Kotlin profile/lifecycle/network routing, pinned trust and authenticated loopback SOCKS. Its actual DNS carrier is sibling `slipstream-rust/`, with effective adaptive-idle, FlowRelay, battery scheduling and multipath bootstrap patches already incorporated. `owenclave/bin/lib/slipstream/build.sh` builds that local source non-destructively and emits ignored arm64 `libslipstream.so`. `SLIPSTREAM_SRC` may explicitly select another already-patched local tree; the script never fetches or resets it. Build/cache directories must be outside source; their contents are generated.

Generated Android build output defaults to root `.cache/slipstream-android/`, ignored by Git. This per-checkout path avoids reusing the original cache's CMake source binding to `~/.cache/owenclave-slipstream/src/vendor/picoquic`. `SLIPSTREAM_BUILD` can select another build-only directory; do not point it at source or reuse an incompatible CMake cache.

FlowRelay server source has exactly one owner: `owenclave/bin/lib/slipstream/flowd/`. The Rust `flow_relay.rs` is its client protocol implementation, not a second server.

See the preserved [DNS contract](../owenclave/docs/dns-tunnel.md) and [Android network contract](../owenclave/docs/android-network-routing.md). Existing active multipath-throughput and blocked jitsi-recovery goals retain their original status.

## C experiment
`slipstream-c/` preserves six uncommitted effective changes and the derived picoquic patches. Both C runtime endpoints have experimental TP4 (IDs0–4) capacity. This does not replace Android Rust or prove additive throughput. The 2026-10-30 experiments found no repeatable additive DL gain; restoration statements are historical observations, not fresh live checks.

C and Rust vendor different picoquic revisions. C configure verifies reverse applicability of both already-applied patches via `patch --dry-run`; it never reapplies them. Rust Android patch files remain provenance references only.

## Test certificates
Public certificate fixtures and the Android public trust pin are retained. All imported private fixture keys and the tracked release keystore are omitted. C runtime tests create temporary keys themselves. For manual C/Rust demos generate a temporary pair and point both server and client at its public certificate:

```sh
certdir=$(mktemp -d)
chmod 700 "$certdir"
openssl req -x509 -newkey rsa:2048 -nodes -days 1 -subj /CN=slipstream-test \
  -keyout "$certdir/key.pem" -out "$certdir/cert.pem"
# Rust interop/bench: CERT_DIR="$certdir" ./scripts/interop/run_rust_rust.sh
# Server: --cert "$certdir/cert.pem" --key "$certdir/key.pem"
# Client: --cert "$certdir/cert.pem"
# Remove the temporary directory when the local test ends.
```

Use `CERT`/`KEY` for `scripts/verify_ipv4_listener.sh` and `--cert`/`--key` for the high-CPU reproduction. Vendored picoquic upstream certificate-specific tests require their own temporary fixtures; they are not the migration's native application gate. Never replace production Android pins with generated test certificates.
