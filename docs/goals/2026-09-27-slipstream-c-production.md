# Goal: permanent C Slipstream server with unchanged APK

> Historical operations record; original paths and observations remain below. [Current boundary](../operations-boundary.md) · [current source architecture](../architecture.md). This record is not fresh deployment authorization or live verification.

Status: complete
Source: user request to permanently replace the server, preserve old connection credentials and require no APK changes; explicit implementation approval.
Last updated: 2026-09-27

## Objective and execution directive
Replace only the n-de1 production Slipstream server with the tested C fork. Preserve the existing certificate/key, Flow token, reset seed, suffixes, listener, FlowRelay/WARP boundary and Android client. Complete the required outcomes using the smallest change; stop after production acceptance and documentation.

## Frozen contract
- R1: support the existing production CLI in C, including explicit IPv4 binding and read-only Rust-format reset seed loading.
  - Evidence: release tests and Rust-client/C-server exact-byte interoperability.
  - Status: verified
- R2: permanently install/start C at `/usr/local/bin/slipstream-server`, with timestamped Rust rollback and unchanged connection credentials.
  - Evidence: installed binary hash, active enabled units, protected before/after file comparison.
  - Status: verified
- R3: verify unchanged Rust client TCP and UDP through production FlowRelay/WARP, stable restart counters and null output; document runtime, rollback and limits.
  - Evidence: bounded authenticated probes, listeners, service state, affected node documentation.
  - Status: verified

## Constraints and change envelope
- Native CLI/socket loop and nearest tests in `tmp/slipstream-c`; existing production binary on n-de1; node documentation and this goal.
- No APK, Android source, DNS delegation, certificate, token, seed, FlowRelay binary/unit, WARP service, host route or firewall change. Production unit can remain unchanged because C implements its existing flags.
- No traffic logs, credentials in output/Git, or new permanent services. Temporary acceptance files are removed.
- Previous A/B goal is complete and historical. Its C-client/C-server loaded-upload improvement is not a promise for unchanged Rust APK/C-server. User explicitly accepts the measured download trade-off; this migration does not claim a new Android benchmark.

## Current checkpoint
- Complete. Production C remains active; no further migration work is required.

## Evidence and decisions
- C2 research source: `bcbf0f53efefe916e22eadf56e52e011c24125d5`, branch `feat/rust-parity-ab`.
- Production-compatible C release SHA256: `b7c6b7ce4905adde4108b820115f1fce538d4afa79782fba6745203e5d2b3702`.
- Added explicit native bind and read-only seed parsing; four release suites pass, including malformed/missing seed rejection and unchanged seed inode/content/mode/mtime.
- Preflight: Slipstream/Flowd inactive+enabled, null output, NRestarts0; WARP active. Initial Rust binary SHA256 `d7667b1e6dde9ef4669cda08f576307d851b3ec8e32736ebdeabae4e9926191b`.
- Unchanged Rust Linux client passed two reused exact-byte gates against C: parallel streams through both suffixes and 512 KiB upload/independent half-close. Harness needed explicit Rust loopback flag, disabled client logging and its expected SIGTERM exit handling; payload assertions themselves passed.
- Protected backup: `/root/backups/slipstream-c-production-20260927T112341Z/`. All credentials and retained runtime files are compared internally without exposing their values or hashes.
- Candidate started with production certificate/seed and identical resource/CLI settings on loopback test port; unit verification passed. Atomic binary replacement completed. Existing production unit required no edits: native C now supports its flags.
- Slipstream and Flowd active+enabled, NRestarts0/0, null output; public `195.128.101.186:53/udp`, private `warpns` `10.200.0.2:40001/tcp`. Certificate/key/token/seed, both units, Flowd binary and persistent firewall are byte-identical to preflight. WARP/interface/host default route unchanged.
- Production probe harness startup failures: missing imported `backend.py`, then `/run` mounted `noexec`. Added its existing helper and executed temporary Rust client from protected backup; no server or system mount change was needed.
- Final production probes: Rust-client TCP with TLS validation and `warp=on`, transaction-exact DNS UDP/UoT, each through direct authoritative and Yandex TCP recursive carrier; second suffix direct TCP WARP; wrong Flow token rejection. All six checks passed. No secondary recursive delegation or Android/LTE performance claim is made.
- Published compatibility change: `d7cd5555a88933053551128ff8b3741ae93049a0` on `feat/rust-parity-ab`; remote branch SHA verified. Server scheduling/credit/transport tuning was not changed by this follow-up.
- Closure: same installed binary hash; all retained files except server binary byte-identical; Slipstream/Flowd restart growth0; WARP activation timestamps/counters unchanged. Removed `/run/slipstream-migration` and temporary Rust probe executable. Private namespace SSH `:41924` and retired TLS `:40002` absent; MasterDNS/flowtls inactive.

## Completion
- R1–R3 verified. Source tests four suites, independent reused Rust/C interoperability gates two tests, production authenticated probes six checks, unit verification and protected file/state comparisons passed.
- Only the server binary was replaced remotely; no production configuration edit was necessary after implementing its existing CLI. Existing enabled services are now running. README/node guide record current C boundary and exact Rust rollback.
- New Android/LTE benchmark remains unperformed: no promise of the previous C/C upload improvement for the unchanged Rust APK.
- Final status: complete.
