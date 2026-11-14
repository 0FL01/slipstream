# Goal: Bound the DNS Tunnel Busy poll burst

Status: complete
Source: user-approved performance plan, 2026-08-13
Last updated: 2026-08-13

## Objective

Compare the current authoritative Busy poll-path burst with one static cap of eight,
using synchronized aggregate-only counters and short production-gVisor payloads, then
retain the cap only if it materially improves throughput and carrier efficiency without
regressing latency, recovery or quiet scheduling.

## Execution Directive

Complete the frozen Required Outcomes using the listed Change Envelope and Primary
Evidence. Work on the smallest unresolved outcome. Do not add requirements from
reviews, tests, tools, speculative risks or optional source text. Finish when every
required outcome is resolved and affected constraints remain satisfied.

## Frozen Contract

### Required Outcomes

- R1: Establish an instrumented current-cap control.
  - Source: user instruction to compare the approved Busy poll-cap treatment with the
    baseline.
  - Acceptance: one temporary aggregate-only build records aligned control deltas for
    ordinary DNS sends, poll-path sends, responses/expiry, adapter admission/completion,
    queue depth and active workers while three exact 1 MiB downloads, one acknowledged
    512 KiB upload and a 4 KiB probe run through the actual gVisor VPN.
  - Primary evidence: exact bytes/status/timings plus cumulative aggregate snapshots
    immediately before and after the scored workload.
  - Status: verified
  - Evidence: the current-cap instrumented control completed exact 1 MiB downloads at
    187.021, 188.705 and 154.824 kB/s (geometric mean 176.132 kB/s), an acknowledged
    512 KiB upload at 13.426 kB/s and a 4 KiB probe in 778 ms. During the scored
    boundary, Rust deltas were 10,796 sends, 10,760 responses, 35 poll-path sends,
    zero expiry and 39 QUIC losses; adapter deltas were 10,792 accepted, 10,757
    enqueued/completed and 35 full drops (0.324%). Control recovery completed an exact
    4 KiB response in 583 ms with one child/VPN.

- R2: Test exactly one additional Busy poll-path cap of eight.
  - Source: approved plan item 2.
  - Acceptance: an otherwise identical build limits only the additional authoritative
    Busy `send_poll_queries` burst to eight; ordinary packet generation, cwnd/pacing,
    Warm/Quiescent/Empty polling and keepalive remain unchanged. The same short workload
    and aggregate boundaries are recorded.
  - Primary evidence: source diff, pinned arm64 build and same-radio Android A/B.
  - Status: verified
  - Evidence: an otherwise identical pinned build capped only the additional Busy
    poll-path burst at eight. Exact 1 MiB downloads were 199.063, 184.144 and 182.954
    kB/s (geometric mean 188.580 kB/s); acknowledged 512 KiB upload was 13.418 kB/s
    and the 4 KiB probe took 706 ms. Scored Rust deltas were 9,845 sends, 9,838
    responses, 58 poll-path sends, zero expiry and 13 QUIC losses; adapter deltas were
    9,847 accepted, 9,840 enqueued/completed and 7 full drops (0.071%). Snapshot skew
    was four packets in control and two in treatment, small relative to each workload.

- R3: Retain only a treatment that passes every gate.
  - Source: user-approved evidence-driven plan.
  - Acceptance: retain the cap only if download geometric mean improves at least 15%,
    adapter full drops are at most 0.1%, DNS expiry and QUIC loss per useful MiB fall at
    least 50%, acknowledged upload is no worse than 5%, 4 KiB latency is no worse than
    10%, cancellation/recovery retains one child/VPN and quiet scheduling is unchanged;
    otherwise restore the current runtime and native artifact.
  - Primary evidence: final normal release payload/egress/Stop acceptance, focused
    source/build checks and stable DE service restart counters.
  - Status: verified
  - Evidence: the cap failed the mandatory throughput gate despite improving carrier
    efficiency, so it and all temporary instrumentation were removed. The normal pinned
    arm64 native artifact was rebuilt at SHA-256
    `785f4de12527f1c2a8311d1325a60ffca994d293d0dc5f146e2e994e07c18eb2` and the APK
    packaged the same library. The installed strict-TCP release retained one child/VPN,
    returned an exact 4 KiB HTTP 200 in 688 ms with `loc=DE`, `warp=on`, emitted no
    throughput aggregates and stopped to zero child/VPN. DE `slipstream.service` and
    `flowd.service` remained active with restart counters unchanged at 5 and 0.

### Constraints

- C1: Keep the strict TCP resolver identity, one resolver/child/path, gVisor capture,
  authenticated FlowRelay and fail-closed WARP egress.
- C2: Temporary instrumentation is in-memory and aggregate-only. Do not record or print
  addresses, QNAMEs, DNS IDs, payloads, tokens, credentials or complete profiles.
- C3: Do not change adapter queue/workers/retries/timeouts, QUIC congestion control,
  server, FlowRelay, WARP, resolver, listener or firewall.
- C4: Preserve Busy timing and all quiet scheduler behavior. Only the additional Busy
  poll-path count may change in treatment.
- C5: Keep each scored runtime window below nine minutes; no 30-60 minute soak.

### Non-goals

- Full data-plus-poll outstanding accounting, resolver fallback, UDP resolver, queue
  growth, worker tuning, pipelining, multipath or broad scheduler/CC work.
- Treating unrestricted LTE as Restricted LTE acceptance.
- Keeping diagnostic telemetry in the final runtime.

## Change Envelope

- Target: pinned Slipstream authoritative Busy poll-path burst and temporary aligned
  counters at its Android TCP adapter boundary.
- Expected paths and symbols:
  - `bin/lib/slipstream/build.sh` and temporary local patches over
    `runtime.rs`, `runtime/path.rs`, `dns/{poll,response}.rs`, `resolver.rs`
  - `SlipstreamInstance.kt::DnsTcpAdapter` and child aggregate drain
  - this goal; stable `docs/dns-tunnel.md` only if a cap is retained
- Direct consumers: generated ignored arm64 `libslipstream.so`, release APK,
  `V2RayInstance` and the installed DNS Tunnel profile.
- Allowed artifacts: temporary aggregate counters and ignored build outputs; one final
  static Busy cap patch only if R3 passes.
- Forbidden artifacts: dependencies, protocol/schema changes, persistent telemetry,
  traffic metadata, services, server changes or extra runtime paths.

## Short A/B Protocol

1. Use the same installed profile, strict TCP resolver, LTE radio/position and fixed
   Cloudflare endpoint. Start a fresh child for each variant and verify DE/WARP egress.
2. Wait for one aggregate snapshot, run one unscored 128 KiB warm-up, then take the
   pre-workload snapshot.
3. Score three exact 1 MiB downloads, one exact 512 KiB POST requiring final HTTP 2xx,
   and one exact 4 KiB download. Record failures without replacement.
4. Take the next aggregate snapshot, cancel four short bulk flows and require an exact
   4 KiB recovery response with one child/VPN.
5. Compare control/treatment deltas and geometric-mean throughput. Stop early when a
   mandatory drop, throughput, upload or latency gate is already impossible.

The previously accepted uninstrumented reference remains contextual only: exact 1 MiB
TCP downloads were 200.017, 199.095 and 192.097 kB/s (mean 197.070 kB/s), acknowledged
512 KiB upload 16.838 kB/s and 4 KiB latency 493 ms. The synchronized control in this
goal is the treatment comparator because LTE varies materially between sessions.

## Current Checkpoint

- Closes: R1-R3.
- Smallest next action: stop; a separate approved goal is required for full outstanding
  accounting.
- Expected evidence: baseline runtime retained with no throughput logs or cap patch.
- Stop or replan if: none; this experiment is terminal.

## Current State

- Resolved: R1-R3. The cap reduced scored full drops from 0.324% to 0.071% and QUIC
  losses from 39 to 13, but download geometric mean improved only 7.1%, below the
  mandatory 15% gate. Upload was neutral and 4 KiB latency improved 9.3%; the cap was
  removed and the normal runtime passed final acceptance.
- Last relevant evidence: final normal release matched the pinned native artifact,
  passed payload/egress/Stop and emitted no diagnostic aggregates.
- Blocker: none.
- Next: stop.

## Material Decisions

- 2026-08-13: cap only the extra Busy poll path at eight; do not reduce the ordinary
  authoritative send loop or alter quiet phases.
- 2026-08-13: use cumulative five-second aggregate snapshots and subtract adjacent
  snapshots around the workload; final instrumentation is always removed.

## Checkpoint History

- 2026-08-13: goal activated from the approved sequence; full outstanding accounting
  remains deferred until this simpler causal experiment closes.
- 2026-08-13: R1-R2 verified. Busy cap eight improved carrier efficiency but not enough
  application throughput; reject it under the frozen all-gates contract.
- 2026-08-13: R3 verified. Cap and counters removed; the rebuilt normal release passed
  one-child payload, DE/WARP egress and Stop acceptance without server restart growth.

## Completion

- Resolved outcomes: R1 control, R2 cap-eight treatment and R3 all-gates decision are
  verified; the treatment was rejected and baseline behavior retained.
- Commands and artifacts: pinned arm64 Slipstream builds, release APK builds/installs,
  exact Termux curl payloads, synchronized aggregate snapshots, native/APK hash match,
  Android child/VPN checks and DE service/restart checks.
- Constraint and diff-scope check: cap, temporary counters, build-script entries and
  temporary files are absent; no runtime, server, resolver or scheduler change remains.
- Final status: complete; cap eight is an efficiency improvement but not a qualifying
  throughput fix.
