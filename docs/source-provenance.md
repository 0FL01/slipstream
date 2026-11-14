# Source provenance

Snapshot imported 2026-11-14 from `/home/stfu/ai/work-table/space`. This is new snapshot history, not a rewrite or transfer of upstream Git history. Source repositories remain in place and unchanged. Exact origins, full revisions, effective `git diff --binary HEAD` SHA256 values and omitted tracked paths are recorded in [source-import.json](source-import.json).

| Local directory | Upstream revision | Effective state |
| --- | --- | --- |
| `owenclave/` | `0FL01/owenclave`, dev `9c8c8799fef8e42c2239ca0bd54fb03afadd737f` | Clean source, whole application/docs; migration changes build path, external signing and scoped guidance |
| `slipstream-c/` | `0FL01/slipstream`, feat/rust-parity-ab `16fdfe97159a3b0b7b13563a671a730923e68c55` | Six working changes preserved, listed below |
| `slipstream-c/subprojects/picoquic/` | `EndPositive/slipstream-picoquic` `2ee2ecd189354791e15cc1ff9db86e4e4b8eba10` | Existing derived path-identity/application-credit patches preserved |
| `slipstream-c/extern/SPCDNS/` | `spc476/SPCDNS` `d291537e1f66b4a7ede069b949a8db0f94c59246` | Effective source |
| `slipstream-c/extern/lua-resty-base-encoding/` | `EndPositive/lua-resty-base-encoding` `bd9246f681806b26469e0d874c893dcab4975aeb` | Effective source |
| `slipstream-c/extern/quick_arg_parser/` | `Dugy/quick_arg_parser` `153b0accc1f0201df9d2fd169bafaeee53e6f300` | Effective source |
| `slipstream-rust/` | `Mygod/slipstream-rust` `bc772dd07d9a136dbd7553b0da575526de207847` | Effective Android-patched `tmp/slipstream-rust-control` source |
| `slipstream-rust/vendor/picoquic/` | `Mygod/slipstream-picoquic` `4bd356c004a50ec46ee8a933ebd024da5d659f75` | Existing Android multipath patch preserved |

## Effective patches
C working changes: `bench/README.md`, `bench/run.py`, `bench/test_bench.py`, `src/slipstream_client_runtime.c`, `src/slipstream_server_runtime.c`, `tests/test_runtime.py`. They include five-path TP4 capacity and benchmark support, not production deployment. Existing derived picoquic changes were copied as files, not reconstructed by resetting its repository.

Rust working changes: `crates/slipstream-client/src/{main.rs,runtime.rs,streams.rs}`, `crates/slipstream-ffi/build/picoquic.rs`, `crates/slipstream-ffi/src/lib.rs`, plus formerly untracked `crates/slipstream-client/src/flow_relay.rs`; effective vendor picoquic changes are included. The original Android patch references remain in `owenclave/bin/lib/slipstream/`: `adaptive-idle.patch`, `flow-relay.patch`, `battery-scheduler.patch`, `multipath-bootstrap.patch`, `picoquic-multipath.patch`. Do not apply them again during a build.

Migration-only adaptations: non-destructive sibling Rust build script; explicit external signing with unsigned release when absent; C patch verifier uses reverse dry-run rather than source mutation; Rust missing-vendor diagnostics describe ordinary vendoring; benchmark instructions use the existing `--binary` override for absent historical baselines; fixture-key instructions use temporary external keys. Runtime source behavior is otherwise preserved.

## Omissions and retention
Excluded tracked release keystore, C/Rust/upstream private fixture keys, generated `.direnv/` environment/cache links, upstream `valgrind_out.txt` output, nested `.git` metadata and `.gitmodules`. Ignored local keystore/signing env, credentials/private configs, build/cache, APK/AAR/SO and historical binary/control builds were never imported. The complete path-level tracked exclusion list is in `source-import.json`. Public certificates, including Android's required trust pin, remain. Source code that parses PEM private-key markers is retained; marker literals are not private key material. Static upstream test vectors such as `binlog_ref.log` and public ESNI records are source fixtures, not runtime logs.

All source licenses and C docs/bench/tests remain. Owenclave's active `2026-09-09-multipath-throughput` and blocked `jitsi-recovery` goal statuses remain unchanged. Only the five approved operations goals and the two approved aggregate evidence sets were transferred; old handoffs/full topology/private workspaces were not copied.

Evidence archives retain byte-for-byte SHA256 identity, and each RU archive member listed by its `aggregate-manifest.json` was verified. Content review found synthetic benchmark aggregates/scripts and restoration proofs, not credentials/private keys or user traffic payloads. Public artifact hashes and the `retired-test-token.jsonl` rejection proof are not token values. Archive scripts are historical evidence only.

All trees are ordinary directories tracked by the single root repository. Upstream nested workflow files are retained but inactive from this root. There is no new remote, push, release or CI activation.
