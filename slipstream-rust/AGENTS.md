# Rust Android carrier

- `crates/` is the Cargo workspace; `slipstream-client/src/flow_relay.rs` implements the authenticated client protocol.
- FlowRelay server belongs only to `../owenclave/bin/lib/slipstream/flowd/`.
- `vendor/picoquic/` is effective vendored source at its own revision. Never unify it with C picoquic or run submodule setup.
- Android patches are already applied; patch files in Owenclave are provenance, not build-time actions.
- Build Android through `../owenclave/bin/lib/slipstream/build.sh` using pinned Rust1.97.1/cargo-ndk4.1.2/NDK29.0.14206865.
- Keep build outputs in ignored target/cache directories. `PICOQUIC_DIR`, `PICOQUIC_BUILD_DIR` and `PICOQUIC_LIB_DIR` remain explicit overrides.
- Public fixtures stay in `fixtures/certs/`; private keys are external temporary fixtures. Read its README before running interop scripts.

## Verify (from this directory)
```sh
cargo test -p slipstream-dns
cargo fmt --check
```

For protocol edits run relevant workspace/interop checks and update `fixtures/vectors/dns-vectors.json`, `docs/protocol.md`, `docs/dns-codec.md` when behavior changes. Android runtime qualification requires a device; native compilation alone does not prove it.
