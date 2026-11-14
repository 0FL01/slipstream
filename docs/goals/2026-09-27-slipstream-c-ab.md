# Goal: C Slipstream fork versus patched Rust

> Historical operations record; original paths and observations remain below. [Current boundary](../operations-boundary.md) · [current C benchmark](../../slipstream-c/bench/README.md).

Status: complete
Source: user request 2026-09-27; approved RECON plan and instruction to begin iterative execution
Last updated: 2026-09-27

## Objective

Deploy `git@github.com:0FL01/slipstream.git` on n-de1, measure the unchanged C baseline against our patched Rust using a local Python runner, port the accepted Rust patches to C iteratively, and commit/push the candidate only if measured benefit is demonstrated. Record successes and failures here. Production downtime is permitted; permanent production promotion is not implicit.

## Execution Directive

Complete the frozen Required Outcomes using the listed Change Envelope and Primary Evidence. Work on the smallest unresolved outcome. Do not add requirements from reviews, tests, tools, speculative risks, or optional source text. Finish when every required outcome is resolved and affected constraints remain satisfied. Preserve evidence of unsuccessful experiments; do not hide failures by changing workloads or excluding timeout rows.

## Frozen Contract

### Required Outcomes

- R1: Deploy and test unchanged C fork baseline on n-de1.
  - Source: user deployment/baseline request.
  - Acceptance: pinned release client/server, remote isolated experiment service, exact bidirectional payload and explicit limitations report.
  - Primary evidence: artifact hashes, service/listener observations and Python payload results.
  - Status: verified
  - Evidence: unchanged HEAD `397850b16e70a52513269134b2c7b9aa13a56745`; remote C0 server SHA256 `109326e404072826b8d8d6cd8fe7d962685659e36dc8232763fdd85f7f822e89`, client `c585e64994191c7015ff8043c0df5af14cc8de9ffb898a0387e28baff2dd8fd8`; all 20 synthetic requests passed in `tmp/slipstream-c/bench/direct.jsonl` campaign `compat-fixed-bind` (4 intentional background cancellations excluded from completed requests). Limitations: raw wildcard TCP client, no pin/auth, single suffix, half-close pending; experiment-only bind shim preserves local system DNS and unchanged artifact.
- R2: Compare C baseline with current accepted Rust through local Python A/B.
  - Source: user local Python A/B and approved plan.
  - Acceptance: fresh Rust control, A/A calibration, balanced pairs with identical workload/carrier/backend budgets; report acknowledged goodput, failures, latency and resources.
  - Primary evidence: aggregate-only JSONL and summary; artifact/config manifests.
  - Status: verified
  - Evidence: fresh Rust client SHA256 `af32d5e5418a7ab2a3c807bf10c6229f13afe709a1ab07ec8c915768e95461f7`; independent 49 unit tests + DNS vector gate; C0/R A/A, balanced transport and corrected WARP screens, six alternating transport confirmation pairs. Aggregate artifacts `tmp/slipstream-c/bench/{final-screen,corrected-warp,confirmation}.jsonl` and `resumed-summary.json`; published secret-free `bench/RESULTS.md`. All failures retained; only failed control primary invalidates its primary ratio.
- R3: Port accepted Rust patches to C and verify behavior iteratively.
  - Source: user port request and approved patch inventory.
  - Acceptance: forced-path/CID guard, FlowRelay/authenticated SOCKS + pinning, adaptive idle/activity scheduler and bounded multipath bootstrap; targeted tests and iteration screens. Preserve dual server suffix boundary in final candidate. Fix reproduced half-close incompatibility if needed.
  - Primary evidence: focused tests, candidate patch/source diff and A/B rows.
  - Status: verified
  - Evidence: native C source plus two reproducible picoquic patches; independently rerun release 4/4 and ASan/UBSan 4/4 suites (12 runtime cases), Python 9/9. Real FlowRelay/WARP acknowledged pinned TCP 8192 bytes and exact native UoT 99-byte echo; wrong password/token/pin rejected, dead-primary/live-secondary and both-dead fail-closed passed. Both server suffixes accepted directly. Secondary recursive failure was localized to missing external NS delegation, not a server suffix rejection; no DNS-zone change was authorized or made.
- R4: Determine benefit and conditionally commit/push.
  - Source: user "коммит пуш если есть профит"; approved plan.
  - Acceptance: publish candidate to a feature branch only on demonstrated benefit; otherwise retain evidence without candidate push. Report C1/C0 and C1/Rust separately.
  - Primary evidence: six fresh balanced confirmation pairs for a promising scenario, focused correctness gates and remote branch SHA if published.
  - Status: verified
  - Evidence: preselected transport-only 1MiB download C2/C0 paired median +21.96%, 6/6 wins, loaded p95 +6.58%, zero candidate payload failures. Published experimental branch `feat/rust-parity-ab`, commit `bcbf0f53efefe916e22eadf56e52e011c24125d5`; `git ls-remote` confirms identical remote SHA. Acceptance is scenario-specific: WARP C2/C0 loaded p95 +24.26% is NOT accepted, C2 does not beat Rust on download, and no permanent deployment is promoted.
- R5: Restore remote state and document campaign outcomes.
  - Source: approved rollback plan and user goal/evidence request.
  - Acceptance: experimental services/listeners/firewall/fixtures removed; production binaries/units preserved and initial inactive state restored; goal contains findings and success/failure checkpoints.
  - Primary evidence: remote closure observation, hash comparison, goal history and `git diff --check`.
  - Status: verified
  - Evidence: experiment units, `/opt/slipstream-ab`, fixture keys/tokens and temporary nftables rules removed. Production binaries, both units and `/etc/nftables.conf` equal protected backup hashes. Slipstream/Flowd inactive+enabled, NRestarts0, null output; WARP active with unchanged activation timestamps/NRestarts, host default via eth0. Public UDP53, private40001 and fixture40100/40004 absent. Local ephemeral credentials removed. Backup retained at `/root/backups/slipstream-ab-20260927T083324Z`.

### Constraints

- Secrets, private keys, tokens, full configs and traffic payloads must not be printed or committed. Existing credentials stay protected; test credentials are ephemeral. Local client secrets go through stdin, never argv/env.
- No retained native traffic output, key logging or qlog. Synthetic-only results contain counts/timing/hashes, not QNAMEs or peer metadata.
- Keep WARP isolated; do not restart WARP or change host routing. Public experiment target is fixed private backend/FlowRelay, not a public unauthenticated proxy.
- Only n-de1 runtime is in scope. RU/SW/router/Android runtime and unrelated Jitsi/OneXray source are untouched.
- Local Owenclave is dirty with Jitsi recovery changes. Never reset its tree or the existing modified Rust cache.
- Backup before remote edit; validate unit/firewall/build before start; only affected services may restart. Return Slipstream/Flowd to initial inactive state.
- No forced push, amend, secrets in Git or commits of unrelated files. Picoquic change must be reproducible from tracked code/patch, not a dirty-only submodule.

### Non-goals

- Android release migration or LTE acceptance. Local Linux measurements are not LTE claims.
- Permanent C promotion, broad transport redesign, extra ARQ, multiprocess pools or rejected domain sharding.
- Port `multidomain-experimental.patch`: it is excluded from accepted Rust build.

## Benefit criterion (approved plan)

Either repeatable paired-median acknowledged goodput gain >=10% in a preselected primary workload, with >=5/6 pair wins, or a substantial reduction of idle query/CPU cost without material goodput/recovery loss. New payload failures, crashes, auth/pinning regressions fail acceptance. Loaded p95 regression <=10%; larger throughput/latency trade-off is reported separately, not silently accepted. A gain against C0 alone does not demonstrate beating Rust. Failures remain in evidence (candidate timeout contributes zero acknowledged throughput, invalid control ratio is undefined).

## Copy of approved execution plan

### 1. Control and release artifacts

Create separate C checkout with SSH origin, pin HEAD/submodules/build flags and hashes. Build unmodified C release client/server. Build Linux Rust from `bc772dd07d9a136dbd7553b0da575526de207847` plus accepted Owenclave patches; use preserved deployed DE Rust binary as server control. Use Debian 12-compatible server build environment. Record durable state in this goal.

### 2. Switchable remote experiment

Use `/opt/slipstream-ab/releases/<variant>/`, `/etc/systemd/system/slipstream-ab.service`, backup `/root/backups/slipstream-ab-<timestamp>/`. Backup existing binaries/units/drop-ins/firewall/service states before switching. Own public UDP53 sequentially with existing Rust or experiment C; preserve certificate, non-root execution, null output and resource bounds. Baseline C uses main suffix `t.x.ass-peak.de`. Rollback stops experiment and restores files plus initial inactive production state; WARP remains untouched.

### 3. Functional compatibility

Check exact bidirectional bytes, independent streams, half-close and recursive DNS. Probe Rust/Rust, C/C, Rust/C, C/Rust; only use compatible cross-pairs for attribution. Python can prepend FlowRelay OPEN to C raw stream without changing baseline C. Isolate raw listener from external access. Record baseline missing pinning/features explicitly.

### 4. Local Python A/B

Variants R (accepted Rust), C0 (unmodified C), C1 (ported C). Same host, suffix, resolver transport, worker/queue budget, workload and backend. TCP DNS via shared Python adapters: single 56 workers/64 queue; explicit multipath 2x28 workers/32 queue using Safe 77.88.8.88 and Basic 77.88.8.1; DCUBIC explicit.

Two levels: transport-only same private synthetic DE backend, then same FlowRelay/WARP path with exact download and remotely acknowledged upload (authenticated temporary fixture). Workloads: sequential download/upload, 8/32/128/512 KiB uploads, upload under continuous download, two parallel streams, idle/recovery. Metrics: acknowledged goodput/completion/failure, startup, p50/p95/max latency, DNS counts/bytes/drops, CPU/RSS/threads, idle query rate.

A/A first, two screening pairs, six fresh AB/BA confirmation pairs for promising result. Do not discard timeouts or count local writes as received payload. Historical LTE results are context only.

### 5. Incremental port

1. Forced-path identity/CID guard: loss/abandonment tests, no crashes, response path identity.
2. FlowRelay/SOCKS + certificate pin: stdin credentials, authenticated SOCKS, TCP/UDP framing, wrong-token/wrong-pin fail-closed.
3. Adaptive idle and activity phases: Busy/Warm/Quiescent/Empty polling and keepalive, idle/recovery/resource evidence.
4. Multipath bootstrap: bounded endpoint attempts, dead-primary/both-dead, correct connection teardown.

Fix half-close only if reproduced. Keep final dual-suffix server boundary. Test and screen each logical step against preceding candidate. Package picoquic fix as tracked parent patch/build integration or reachable submodule commit.

### 6. Decision, publication and closure

Apply benefit gates above; report C1/C0 and C1/R independently. If accepted, commit/push to feature branch `feat/rust-parity-ab` with tests/harness and concise results. If no benefit, restore remote and report without pushing candidate. Permanent runtime promotion is separate.

## Change Envelope

- C fork: separate checkout (planned `tmp/slipstream-c`), `src/`, `include/`, Meson build integration, tracked picoquic patch, focused tests and Python A/B harness.
- Rust: separate pinned source/build scratch only; accepted `owenclave/bin/lib/slipstream/{adaptive-idle,flow-relay,battery-scheduler,multipath-bootstrap,picoquic-multipath}.patch` read-only.
- Ops: this goal; node docs only if final stable boundary changes.
- Remote: experiment releases/unit, protected timestamped backups, ephemeral private fixture/service; existing production artifacts preserved. Any firewall changes scoped and backed up.
- Evidence/artifacts ignored locally; only secret-free summary/harness can be published.

## Current Checkpoint

- Closed: R1–R5. No further implementation or runtime promotion is authorized by this goal.
- Final evidence: scenario-specific transport confirmation, published branch SHA, release/sanitizer/harness gates, fresh checkout build/test and verified remote rollback.

## Current State

- C baseline HEAD `397850b16e70a52513269134b2c7b9aa13a56745`; picoquic `2ee2ecd189354791e15cc1ff9db86e4e4b8eba10`.
- Rust source pin `bc772dd07d9a136dbd7553b0da575526de207847`; picoquic `4bd356c004a50ec46ee8a933ebd024da5d659f75`.
- DE Rust binary SHA256 `d7667b1e6dde9ef4669cda08f576307d851b3ec8e32736ebdeabae4e9926191b`; Flowd `eb07424fe9cee1102c136ec6ef6884388e93806174816ce5d3b3906bcd511a6e`.
- Final state equals initial: production units inactive/dead, enabled, NRestarts0, null stdout/stderr. WARP units active and namespace interface exists. No public UDP53 or private40001 listener. Experiments removed.
- DE production server configured for `195.128.101.186:53/UDP` -> `10.200.0.2:40001`; dual suffix; max connections32, idle30s, memory256MiB, CPU100%, tasks128; unprivileged slipstream user with bind capability only.
- Local Docker image `owenclave-multipath-build:bookworm` exists. DE gcc/g++/make/pkg-config exist; Meson/CMake/Ninja/OpenSSL headers absent. Local gh absent (read-only GitHub API used for RECON).
- C baseline raw TCP binds INADDR_ANY; no cert-pin CLI; only one domain. C picoquic has unguarded forced-path index after abandoned-path deletion. Homogeneous compatibility verified; failed cross-pairs preserved and not used for attribution.
- Blocker: none for the completed contract. Full-WARP performance is not accepted; secondary recursive suffix lacks external delegation. Neither is represented as a successful path or a production recommendation.
- C2 native parity implemented; independent rerun `meson test -C build-candidate --no-rebuild` passed 4/4 (12 runtime cases, protocol/path/pin suites); agent sanitizer 4/4. Independent Rust 49 unit tests + DNS vectors passed. Two unchanged upstream Clippy style warnings retained, not patched.
- Remote protected backup `/root/backups/slipstream-ab-20260927T083324Z` retained; production files untouched. All experiment targets/services removed after exact-byte acceptance; no public unauthenticated forwarding or native retained output was enabled. WARP untouched.
- Harness under `tmp/slipstream-c/bench/`; local credentials deleted, raw aggregate JSONL remains ignored. Native artifacts remain local. Meson intentionally applies tracked patches to the pinned picoquic submodule; its derived dirty state is reproducible, not an unpublished dependency commit.

## Material Decisions

- 2026-09-27: Approved plan begins; downtime allowed, permanent promotion excluded; isolate source/builds and preserve unrelated dirty trees.
- 2026-09-27: C0 cannot bind a chosen IPv4 address; experiment-only LD_PRELOAD shim rewrites only wildcard IPv4 bind :53 to DE public IP for C variants. No baseline source/binary modifications, no systemd-resolved interruption.
- 2026-09-27: Publish the confirmed transport-only benefit in an explicitly experimental feature branch, not as full-WARP acceptance. Preserve the failed WARP latency gate and slower download versus Rust in the published results; permanent migration remains outside scope.

## Checkpoint History

- 2026-09-27 RECON: GitHub HEAD/submodule and exact live service state confirmed read-only. No deployment changes. Missing local gh and remote build dependencies have safe alternatives (Git/API and Debian build container); no external blocker.
- Build failure: baseline Meson 1.0 CMake import omitted picotls transitive libraries. Relinking unchanged objects with picotls archive group succeeded; candidate fixes dependency propagation. LTO upstream picohash warnings recorded, not suppressed as correctness evidence.
- Deployment failure: protected umask made experiment directories root-only (`203/EXEC`, backend exit2); assigned slipstream group and 0750 directories. Next failure exit27: wildcard :53 collided with systemd-resolved. Fixed binding via narrow shim; local resolver unchanged. Failed startup row retained (`compat`); subsequent all-payload success `compat-fixed-bind`, sequential 1MiB acknowledged download 550965 B/s. This first sample is functional evidence, not benefit proof.
- Compatibility: R-client/C0-server transferred exact payload (direct 1MiB 3.55 MB/s); C0-client/R-server failed after startup. C2-client/C0-server recursive primary passed (~341152 B/s), but recovery reset; C0-client/C2-server reset after the first exchange and its process disappeared. These cross-pairs cannot substitute for homogeneous attribution. Baseline limitations are observed, not patched into C0.
- Fresh recursive A/A: C0 primary 305989/303392 B/s (0.85% variation); R-client/deployed-R-server 408417/394842 B/s (3.32%). Full-window audit additionally found C0 loaded-upload `IncompleteReadError` (2 failures per A/A window); primary success does not mean whole-window success. Rust windows had no non-cancellation failures. All failed rows retained.
- C1 regression: primary ~249516/224807 B/s in initial single screens; loaded-upload successful p95 ~14.85 s, against baseline successful p95 ~11.25 s. No publication justified. These single screens are not balanced proof.
- C2 causal fix: advance bounded receive credit after 16 KiB rather than 64 KiB drain, preserving 128 KiB receive window/256 KiB output cap. Local 100 ms relay increased 1MiB goodput ~37%; DE C2 screens 357610/373899 B/s, no non-cancellation payload failures. First screen's position tag was supplied incorrectly; actual execution order was B then A. Preserve original evidence; do not treat malformed/incomplete pairs as confirmation.
- Rejected C3 experiments: busy burst 16->40, Rust-equivalent adaptive/default receive credit, and removal of target cap did not improve local loaded upload (~8.75 s control). Reverted native trial changes. Local fixture did not reproduce DE's C2 ~15.97 s loaded latency, so it cannot establish a DE fix. Tests/probe refinements retained; no candidate promotion or push.
- Balanced screens: transport C2/C0 primary approximately +16%, loaded p95 +9.02%; C2/R primary -3.84%, loaded p95 -55.50%. Known C0 loaded failures remain distinct from acknowledged primary ratios. Initial WARP windows timed out on every arm; no valid ratios inferred.
- WARP fixture failure: exact source IP from Cloudflare trace did not remain valid per destination/connection. A short fixed-port discovery probe first failed due to an embedded null in a subprocess argument; its temporary rule was removed in `finally`. Corrected probe and RDAP verified Cloudflare ownership; provider-source/fixed-destination authenticated fixture then passed independently pinned TLS from a socket bound to `CloudflareWARP`. Test tokens rotated, no production credentials changed. No host routing/WARP restart or persistent firewall edit.
- Corrected full-path WARP screens: C2/C0 paired primary +11.07%, 2/2 wins, loaded p95 11.9005 -> 14.7873 s (+24.26%, gate FAIL); C2/R primary -7.69%, loaded p95 22.6689 -> 13.6633 s (-39.73%). C2 and Rust had zero unexpected failures; C0 had nine parallel/background `IncompleteReadError` failures. WARP performance acceptance was not granted.
- Six fresh transport AB/BA confirmation pairs: ratios 1.413295, 1.304601, 1.034003, 1.200656, 1.238587, 1.116145; paired median +21.96%, 6/6 wins. Loaded p95 11.5605 -> 12.3211 s (+6.58%, pass), identical including failed rows and successful-only. All primary requests passed; C2 6/6 full windows, C0 1/6, with seven baseline loaded failures retained. C2 CPU/window rose 1.445 -> 1.90 s; quiet-open DNS rate increased, so no overall idle/CPU benefit claimed.
- Live acceptance: pinned/authenticated TCP and native UoT through experimental interface-bound FlowRelay/WARP exact; negative auth/pin/token and bounded bootstrap gates passed. Secondary suffix recursive reset remained in `acceptance.json`; authoritative `aiden.ns.cloudflare.com` returned main-suffix NS delegation and secondary-suffix authoritative NXDOMAIN. Both suffixes passed direct authoritative C2 acceptance. Existing DNS-zone limitation is documented, not patched around.
- Publication gates: independent release 4/4, ASan/UBSan 4/4 and Python 9/9 passed. Patch-file whitespace context corrected without changing native code; reverse application and idempotent configure verified. Only explicit source/tests/harness/aggregate summary staged; credentials, certs, raw JSON, binaries and Python caches excluded. Commit/push `bcbf0f53efefe916e22eadf56e52e011c24125d5` verified remotely; no PR or production promotion.
- Rollback: stopped only four experiment units; deleted their files/drop-ins, fixture firewall rules, releases, fixture keys and ephemeral tokens. Production binary/unit/persistent nftables hashes compared equal to backup both before and after cleanup. Slipstream/Flowd inactive+enabled, null output/NRestarts0; WARP activation timestamps and NRestarts unchanged, interface remains in warpns, host default direct. No experiment listener remains. Local temporary credentials removed.
- Published-build reproducibility: fresh detached worktree of `bcbf0f5` initialized original pinned submodules; tracked patches applied successfully from clean dependency source. First Docker configure failed because linked-worktree Git metadata was outside its mount, not because of the patch or native code. Mounting the sibling checkouts at their actual paths resolved it; fresh Meson release configure/link and 4/4 suites passed. Upstream CMake ownership/version warnings and unchanged LTO picohash warnings remain recorded, not suppressed as validation.

## Completion

- Resolved outcomes: R1–R5 verified. Conditional publication is supported by six confirmed transport pairs, not by a full-WARP win.
- Commands and artifacts: `meson test -C build-candidate --no-rebuild --print-errorlogs` and `build-asan` (4/4 each); `python3 -m unittest discover -s bench -p test_bench.py` (9/9); `python3 bench/final_report.py --resumed`; real TCP/UoT/auth/pin/bootstrap checks; fresh published checkout Meson release build + 4/4; reverse/idempotent picoquic patch checks; `git diff --check`, secret scan and remote branch SHA verification. Rust unit/DNS gates 49+1 passed; strict Clippy had two unchanged upstream style warnings.
- Published result: https://github.com/0FL01/slipstream/tree/feat/rust-parity-ab ; commit `bcbf0f53efefe916e22eadf56e52e011c24125d5`; full secret-free summary `bench/RESULTS.md`. Goal and ignored raw aggregate evidence preserve successful and unsuccessful attempts.
- Constraint and diff-scope check: only C fork native/build/tests/harness and this operations goal changed; no unrelated source/runtime reset, no permanent migration, no secrets/raw traffic evidence committed, no force-push/amend. Production state and hashes restored, WARP isolation preserved. The operations repository goal is left uncommitted; source-fork commit/push was explicitly requested.
- Final status: complete. Confirmed improvement is transport-only versus C0. WARP loaded latency +24.26% fails acceptance, Rust retains higher primary goodput, and the secondary recursive suffix's missing delegation remains a documented pre-existing limitation outside the server port.
