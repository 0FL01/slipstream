# C Slipstream experiment

- Native runtime lives in `src/`; `tests/test_runtime.py` covers transport lifecycle and multipath.
- `bench/` owns synthetic foreground experiments and existing explicit `--binary` overrides.
- `subprojects/picoquic/` and `extern/` are vendored effective source, not submodules. `patches/apply-picoquic-patch.py` verifies existing patches without writing source.
- Keep C picoquic separate from `../slipstream-rust/vendor/picoquic/`.
- TP4/five-path capacity is experimental; endpoint activity is not proof of additive useful throughput or fifth-only loss recovery.
- Android still uses Rust. FlowRelay remains in Owenclave, never duplicated here.
- No private demo key ships. Use temporary certificate/key pairs; see `../docs/architecture.md`.

## Verify (from repository root)
```sh
python3 -B -m unittest discover -s slipstream-c/bench -p 'test*.py' -v
meson setup slipstream-c/build-candidate slipstream-c
meson compile -C slipstream-c/build-candidate
meson test -C slipstream-c/build-candidate --print-errorlogs
```
