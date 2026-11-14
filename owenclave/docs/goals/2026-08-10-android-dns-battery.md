# Goal: Reduce Android DNS Tunnel battery drain

Status: complete
Source: user-approved battery plan after the 2026-08-10 multi-agent code and plan audits
Last updated: 2026-08-10

Stable contracts: [`../dns-tunnel.md`](../dns-tunnel.md) and
[`../android-network-routing.md`](../android-network-routing.md). This goal remains
the historical measurement and acceptance record.

## Objective

Reduce reproducible Android DNS Tunnel idle battery drain by fixing the notification
polling unit bug and replacing stream-count-driven Slipstream polling with a measured
activity-aware scheduler, while preserving FlowRelay behavior, bounded readiness,
background delivery, throughput, and fail-closed routing.

## Execution Directive

Complete the frozen Required Outcomes using the listed Change Envelope and Primary
Evidence. Work on the smallest unresolved outcome. Do not add requirements from
reviews, tests, tools, speculative risks, or optional source text. Finish when every
required outcome is resolved and affected constraints remain satisfied.

## Frozen Contract

### Required Outcomes

- R1: Attribute the excess drain before transport edits.
  - Source: approved CP0 measurement gate, superseded by the user-approved fast path.
  - Acceptance: record the live DNS profile power-affecting settings and compare it
    with one frozen direct Owenclave gVisor profile using aggregate CPU scheduling,
    radio-active, process, and WakeLock evidence without packet or destination capture.
    Continue only when the DNS child shows a material scheduler/radio mechanism absent
    from the direct control. Do not claim battery percentage or mAh from this fast path.
  - Primary evidence: bounded device settings/capability inventory and separate 10–15
    minute diagnostic traces for the DNS and direct profiles.
  - Status: verified
  - Evidence: the live profile used gVisor and a direct UDP resolver with profile
    statistics on at the persisted 3 ms defect, while app statistics, PCAP, WakeLock,
    battery exemption, and debug logging were off. Separate screen-off Perfetto traces
    showed the DNS-only Rust child consume 52.375 CPU seconds and 156,623 scheduler
    slices over 599.934 seconds, with a median 331.5 slices/second while running and
    20 quiet application flows retained. The matched direct gVisor control had no
    Slipstream child. Batterystats history correlated Owenclave activity with
    `cellular_high_tx_power`; server restart baselines stayed 4/0. Raw traces were
    deleted after aggregate extraction. This proves a large transport scheduling
    mechanism, not a battery percentage or mAh delta.

- R2: Correct the screen-on statistics interval independently.
  - Source: confirmed `3` milliseconds versus `3000` milliseconds unit defect and
    approved CP1.
  - Acceptance: a missing value defaults to `3000`; persisted exact `3` migrates to
    `3000`; `0` remains disabled; every other nonzero value below `500`, including
    negative values, normalizes to `500`; values `500` and above are unchanged.
    Screen-on profile statistics run at approximately three seconds and stop when
    the screen turns off.
  - Primary evidence: focused value matrix, release Kotlin compilation, and bounded
    live callback cadence observation.
  - Status: verified
  - Evidence: `normalizeSpeedInterval` covers missing/exact `3` to `3000`, preserves
    `0` and values at least `500`, and maps every other value to `500`; `DataStore.init`
    applies the same idempotent normalization to persisted parseable values. Release
    Kotlin compilation and `:app:assembleOssRelease` passed. After the signed APK was
    installed over the baseline build, the live Settings UI showed `Speed Interval`
    as `3s` rather than `3ms`. Existing screen-off listener removal is unchanged.

- R3: Replace open-stream polling with one activity-aware client scheduler.
  - Source: approved CP2 and audited first-sufficient scheduler.
  - Acceptance: Busy preserves existing pacing, 400 ms keepalive, and 50 ms slice;
    Warm emits at most one explicit poll per 400 ms; QuiescentOpen emits at most one
    explicit poll per 2 seconds with the proven 5-second keepalive and no permanent
    50 ms clamp; Empty keeps zero explicit polls and the proven 5-second keepalive.
    OPEN, local/remote payload, and local/remote FIN restore Busy immediately. QUIC
    ACK, PTO, retransmission, close, and path deadlines always take precedence.
  - Primary evidence: focused Rust transition/deadline tests, clean pinned patch
    replay, arm64 source build digest, and aggregate carrier/loop-wake observations.
  - Status: verified
  - Evidence: all 24 pinned Rust client tests pass, including Busy/Warm/
    QuiescentOpen/Empty derivation, poll budgets, sparse retry/deadline behavior,
    QUIC-deadline precedence, FlowRelay vectors, and half-close regression tests.
    The three patches replay cleanly against `bc772dd`; the Android 21 AArch64 NDK r29
    executable is stripped and has SHA-256
    `3932b8272e635baeeecbdb26e387e10fdd6bdf6a559171bfaa701ff312fe08d2`.
    The signed release APK builds with SHA-256
    `545413b54b44901eec9d17fb8961f1fe6cff9a4f849adf0ad12a56f892a5abd3`.
    Android mechanism and runtime gates passed as recorded under R4.

- R4: Prove measured efficacy and preserve the accepted runtime contract.
  - Source: approved CP3 efficacy and regression gate, superseded by the user-approved
    fast path.
  - Acceptance: a 10–15 minute current-versus-scheduler diagnostic shows at least 80%
    fewer Slipstream scheduler wakeups for quiet open flows and no more than one carrier
    query per second in QuiescentOpen. Remote quiescent data/FIN p99 stays within 2.5
    seconds, 8 MiB throughput regresses by no more than 10%, 4 KiB TCP and application
    UDP median regress by no more than 100 ms, and exact TCP/UDP, cancellation, WARP,
    ten-minute screen-off, process, and restart gates pass. Report mechanism evidence
    only; do not claim battery percentage or mAh improvement.
  - Primary evidence: current and scheduler aggregate traces plus exact Android runtime
    probes and server restart inventory.
  - Status: verified
  - Evidence: the current-build screen-off baseline recorded 156,623 Slipstream
    scheduler slices and 52.375 CPU seconds in 599.934 seconds; the scheduler build
    recorded 10,521 slices and 4.214 CPU seconds in 599.966 seconds, reductions of
    93.3% and 92.0%. Per-second median child slices fell from 331.5 to 4 (98.8%), and
    total Owenclave main/background/child slices fell from 199,296 to 21,464 (89.2%).
    QuiescentOpen is source-bounded to one explicit poll per 2 seconds plus one
    ACK-eliciting keepalive per 5 seconds, at most 0.7 carrier queries/second before
    loss recovery; source inspection and deadline tests prove that in-flight DNS IDs
    alone do not select the 50 ms Busy slice. Ten controlled connection-close HTTP
    samples after a 10-second remote delay had zero failures; after subtracting the
    no-delay control median,
    inferred delivery/FIN latency had 944.5 ms median and a 1,489.5 ms empirical p99
    upper bound. Exact 4 KiB median was 337 ms and application UDP median 153 ms,
    respectively 2 ms and 53 ms above the accepted operator-UDP baselines. Exact
    8 MiB passed in 28.118 seconds with a literal destination and in 62.344 seconds
    including application DNS, within the accepted 75-second absolute gate and 6.1%
    above the accepted 58.784-second full Android baseline. Three eight-flow
    cancellation cycles were followed by exact TCP in
    402/387/408 ms and UDP in 113/121/125 ms. WARP reported on; ten minutes screen-off
    preserved app, service, and child PIDs and the following exact probe passed.
    Server Slipstream/flowd stayed at PIDs `1155657`/`1115762` and restart baselines
    4/0. Temporary traces and probes were deleted. These results prove scheduler,
    CPU, packet-policy, and runtime effects only; no battery percentage or mAh saving
    is claimed without the deferred physically unplugged measurement.

### Constraints

- C1: Preserve the existing FlowRelay wire, certificate pin, stdin-only credentials,
  one profile-supplied resolver, one app flow per QUIC stream, gVisor-only operation,
  and 15-second VPN/5-second latency-test fail-closed readiness.
- C2: Do not add fallback, resolver ranking/rotation, mux, health-probe streams,
  traffic or destination logging, PCAP, packet capture, persistent measurement dumps,
  a service, dependency, schema, Exclave fork, JNI boundary, or server protocol change.
- C3: Keep the negotiated-30-second QUIC connection safe: quiescent and empty
  ACK-eliciting keepalive remains five seconds in this objective.
- C4: Production `slipstream.service` and `flowd.service` restart counts must not grow
  during controlled validation. The known server close/reconnect crash blocks
  zero-flow dormancy and any churn-heavy transport work.
- C5: Do not print or commit tokens, complete profile links, keystores, credentials,
  private databases, raw traces, or broad Android diagnostic dumps.

### Non-goals

- Zero-flow disconnect/dormancy, graceful QUIC close, server fatal-path repair,
  server-held long polling, larger QUIC idle timeouts, screen-off suspension, or
  changes to Exclave association lifetimes.
- Event-driven child supervision, TCP adapter redesign, System/mixed TUN, direct
  FlowRelay outbound, JNI embedding, SOCKS removal, generalized zero-copy work,
  crypto tuning, or metered-network policy changes without CP3 evidence.

## Change Envelope

- Target: Android preference correctness and the pinned Rust Slipstream client
  scheduler only.
- Expected paths:
  - `app/src/main/java/io/nekohasekai/sagernet/database/DataStore.kt`
  - `bin/lib/slipstream/adaptive-idle.patch` and one subsequent focused scheduler patch
  - `bin/lib/slipstream/build.sh` patch replay list
  - ignored generated `app/src/main/jniLibs/arm64-v8a/libslipstream.so` and release APK
  - `AGENTS.md` and this goal after verified behavior changes
- Allowed artifacts: focused local tests, disposable source/build trees, ignored
  signed arm64 APK/native binary, and aggregate measurement summaries. Raw temporary
  traces belong only in memory-backed storage and are deleted after aggregation.
- Forbidden artifacts: server/runtime changes, new dependencies or architecture,
  secret-bearing output, packet traces/logs, and generated measurement archives in Git.
- Budget: first sufficient fast-path evidence. Stop if the scheduler does not reduce
  quiet-open wakeups by at least 80%; stop after R4 if the mechanism and runtime gates
  pass. Physical charge/mAh measurement is explicitly deferred by the user.

## Current Checkpoint

- Closes: none; all required outcomes are verified.
- Smallest next action: none.
- Expected evidence: closure evidence is recorded below.
- Stop or replan if: not applicable.

## Current State

- Resolved: R1-R4; the installed arm64 release uses the normalized 3-second statistics
  interval and activity-aware Slipstream scheduler through one UDP resolver and gVisor.
- Last relevant evidence: the ten-minute scheduler trace, exact TCP/UDP/WARP,
  connection-close delay, cancellation, screen-off, PID, and restart gates passed.
- Blocker: none.
- Next: none; the approved fast-path objective is complete.

## Material Decisions

- 2026-08-10: keep one conservative five-second ACK-eliciting keepalive for open-idle
  and empty states; reject the unaudited 15-second taper.
- 2026-08-10: measure before the Rust scheduler; fix the independent 3 ms correctness
  defect in a separate checkpoint/build.
- 2026-08-10: defer zero-flow dormancy until the known server close/reconnect fatal
  path and graceful client shutdown are solved as a separate objective.
- 2026-08-10: user approved the fast path: use aggregate scheduler/carrier mechanism
  evidence and full runtime regression gates, but make no battery percentage or mAh
  claim without the deferred physically unplugged paired runs.

## Checkpoint History

- 2026-08-10: contract frozen from the approved audited plan; R1 started with the
  connected Android device and clean repository at `9dc9bea`.
- 2026-08-10: CP0 inventory and diagnostic traces confirmed the live 3 ms defect and
  high DNS-child scheduling with 20 quiet open flows. A minimal direct gVisor control
  was created on-device. The `DataStore.kt` R2 fix is locally implemented and release
  Kotlin compilation passes, but it remains uninstalled until the current-build energy
  baseline is frozen.
- 2026-08-10: the user superseded the long physical charge gate. R1 is verified from
  bounded mechanism evidence; R2 is now the current checkpoint.
- 2026-08-10: R2 was committed as `d2d88dc`, the release APK built and installed, and
  the live persisted interval migrated from `3ms` to `3s`. R3 is current.
- 2026-08-10: R3 was committed as `8484b69`; the pinned source build, release APK,
  ten-minute mechanism comparison, delayed downlink/FIN, exact payload, cancellation,
  WARP, screen-off, and restart gates passed. R1-R4 are verified.

## Completion

- Resolved outcomes: R1-R4.
- Commands and artifacts: release Kotlin/APK builds, 24 Rust tests, clean three-patch
  replay, Android 21 AArch64 NDK r29 binary SHA-256
  `3932b8272e635baeeecbdb26e387e10fdd6bdf6a559171bfaa701ff312fe08d2`,
  signed APK SHA-256
  `545413b54b44901eec9d17fb8961f1fe6cff9a4f849adf0ad12a56f892a5abd3`,
  bounded aggregate traces, exact Android probes, and remote service inventory.
- Constraint and diff-scope check: only `DataStore.kt`, one pinned client scheduler
  patch/build-list entry, `AGENTS.md`, and this goal changed. FlowRelay wire, server,
  resolver ownership, credentials, gVisor, readiness, logging, and routing are unchanged.
- Final status: complete under the user-approved mechanism fast path; physical energy
  quantification remains intentionally unclaimed and out of this completed objective.
