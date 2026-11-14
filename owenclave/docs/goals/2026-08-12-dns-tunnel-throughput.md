# Goal: Diagnose and improve single-resolver DNS Tunnel throughput

Status: complete
Source: user-provided throughput diagnosis and iteration directive, 2026-08-12
Last updated: 2026-08-12

## Objective

Measure a short repeatable baseline for the current fixed TCP-resolver DNS Tunnel,
separate resolver loss, Android adapter saturation and downstream poll underfill with
aggregate-only instrumentation, then keep the first narrow static change that improves
the measured path without regressing latency, recovery, quiet scheduling or fail-closed
behavior. If no listed candidate passes its gate, retain baseline behavior and record
the negative result.

## Execution Directive

Complete the frozen Required Outcomes using the listed Change Envelope and Primary
Evidence. Work on the smallest unresolved outcome. Do not add requirements from
reviews, tests, tools, speculative risks, or optional source text. Finish when every
required outcome is resolved and affected constraints remain satisfied.

## Frozen Contract

### Required Outcomes

- R1: Establish the current fixed-resolver baseline before throughput tuning.
  - Source: user instruction to take a baseline average and record speeds without
    running 30-60 minute tests.
  - Acceptance: three warmed short runs record per-run and arithmetic-mean upload and
    download speed, plus one four-flow/4 KiB latency observation and cancel/recover
    result; the complete measurement window stays below 30 minutes.
  - Primary evidence: installed arm64 baseline APK on the current LTE radio, exact byte counts,
    monotonic timings, per-run rates and arithmetic means recorded in this document.
  - Status: verified
  - Evidence: on LTE with Wi-Fi off, the installed unchanged `bfaf142` release kept one
    child/VPN. Exact 2 MiB downloads completed in 22.903, 24.519 and 16.455 seconds at
    91.566, 85.530 and 127.448 kB/s; arithmetic mean 101.515 kB/s. Three exact 2 MiB
    uploads each sent all bytes but the endpoint returned no final response before the
    50-second bound: each effective lower-bound rate was 41.940 kB/s and is recorded as
    a timeout, not a success. A 4 KiB probe took 796 ms; after four bulk flows were
    cancelled, the same probe completed in 742 ms with one child/VPN retained. The
    measured window was about eight minutes.

- R2: Attribute the dominant baseline limit with aggregate-only instrumentation.
  - Source: user-provided first-step diagnostic plan.
  - Acceptance: one behaviorally neutral build reports only end-of-test aggregates for
    DNS attempted/sent/valid/non-empty/expired/bytes/RTT; TCP adapter accepted/enqueued/
    drops/retries/expired/queue HWM/worker busy; and Busy supply target/all outstanding/
    bytes-in-transit/cwnd plus retransmit/flow-blocked indicators available at the
    pinned client boundary. No address, QNAME, token or payload is logged.
  - Primary evidence: one short fixed-resolver workload and its redacted aggregate
    block, followed by exactly one applicable decision gate below.
  - Status: verified
  - Evidence: one instrumentation-only fixed-resolver run completed an exact 2 MiB
    download at 132.638 kB/s and emitted only aggregate counters. Rust reported DNS
    attempted/sent 782/782, valid 369, non-empty 98, expired 412, response bytes
    19,985 and RTT buckets `<100/100-249/250-499/500-999/>=1000 ms` of
    `0/191/149/29/0`; Busy samples 521, underfill 55 (10.6%), cwnd-limited 161
    (30.9%), target sum 55,875, outstanding sum 202,314, in-transit sum 4,962,579,
    cwnd sum 6,700,610, flow-blocked 0, QUIC sent/lost/timer-losses 765/422/0.
    Adapter reported accepted/enqueued/drop 3,409/2,997/412, retry 25, expired 0,
    queue HWM 65 and worker busy 13.6%. The applicable first gate is adapter drop:
    12.09% exceeds 0.1%; later gates are not selected for this iteration.

- R3: Test narrow static candidates in evidence order and retain only a passing change.
  - Source: user instruction to iteratively try the proposed solutions.
  - Acceptance: each attempted candidate has control/treatment speeds and outcome
    recorded. Stop at the first candidate that passes its listed success gate; if all
    applicable candidates fail, restore baseline runtime code.
  - Primary evidence: short same-workload A/B on the same radio position and endpoint,
    focused source checks, final Android one-child payload/recovery acceptance.
  - Status: verified
  - Evidence: the selected adapter candidate kept 32 workers and increased only queue
    capacity from 64 to 128. Its exact 2 MiB treatment run completed at 136.234 kB/s
    versus the same instrumentation control at 132.638 kB/s (+2.7%), but adapter
    drops increased to 797/3,714 (21.46%) with HWM 129 and no expiry. It therefore
    failed the mandatory zero-drop gate before the longer upload/mixed workload; the
    baseline 32-worker/64-slot runtime was restored. Resolver and poll-supply candidates
    were not applicable because their gates require zero adapter drops.

### Constraints

- C1: Keep one statically selected resolver, one Slipstream child, one carrier path,
  gVisor-only capture, authenticated FlowRelay and fail-closed WARP egress.
- C2: Keep one application flow per local connection and independent QUIC stream; no
  mux, multipath, fallback resolver, ranking service or health stream.
- C3: Preserve Busy/Warm/Quiescent/Empty scheduling and the current cwnd, pacing and
  burst caps unless R2 specifically selects the local Busy outstanding-query deficit.
- C4: Instrumentation is temporary, in-memory and aggregate-only. It must not retain
  traffic metadata or print addresses, QNAMEs, credentials or payloads.
- C5: DNS carrier downtime is allowed in this development environment. Public server
  protocol, listener, firewall, resolver delegation, FlowRelay and WARP configuration
  remain unchanged.
- C6: Do not run a 30-60 minute throughput, idle or soak test. One baseline or A/B
  measurement window must stay below 30 minutes.

### Non-goals

- Multipath, bonding, resolver fallback, health/ranking streams, SSH/smux, aggregate
  streams, direct QUIC/GOST/WebRTC, DoH/DoT, MTU work or broad scheduler/CC rewrites.
- DNS/TCP pipelining, `flowd` DNS cache, zero-copy, I/O rewrite, autotuning or persistent
  telemetry.
- Upstream recovery backport `5424512` and fresh readiness generations: useful
  correctness candidates, but not ordinary throughput candidates for this goal.

## Change Envelope

- Target: current Android fixed TCP-resolver path and pinned Rust Slipstream client.
- Expected paths and symbols:
  - `app/src/main/java/io/nekohasekai/sagernet/bg/proto/SlipstreamInstance.kt`
    (`DnsTcpAdapter` and aggregate handoff)
  - `bin/lib/slipstream/*.patch`, `build.sh`, and pinned client `runtime.rs`,
    `pacing.rs`, `dns/{poll,response}.rs` patch surfaces
  - a minimal foreground measurement harness only if existing UI cannot produce the
    required exact upload/download and cancellation evidence
  - this goal; stable docs only if final runtime behavior changes
- Direct consumers: `V2RayInstance`, the generated arm64 `libslipstream.so`, DNS Tunnel
  benchmark/service child ownership and installed Owenclave APK.
- Allowed artifacts: temporary aggregate counters, ignored native/APK outputs and one
  narrow static adapter or Busy-budget diff selected by evidence.
- Forbidden artifacts: dependencies, schema or protocol changes, services, persistent
  logs, secrets, complete profiles, packet captures, extra runtime paths or server
  deployment changes.

## Short Measurement Protocol

- Network: LTE, Wi-Fi off, fixed device position; record one RU and one external direct
  control before enabling the profile. If external access is unrestricted, the A/B may
  still measure the forced DNS Tunnel path but cannot claim restricted-LTE traversal.
- Resolver: currently selected strict TCP resolver; no candidate scan or fallback.
- Baseline/A-B workload: three warmed repetitions each of exact 2 MiB download and
  upload through the tunnel; report bytes/time and kB/s. Then run four concurrent bulk
  flows with one 4 KiB probe, and cancel bulk before exact TCP/UDP recovery probes.
- Keep the same endpoints and ordering for control and treatment. Record failures and
  partial bytes instead of silently retrying. The 2 MiB size is the short substitute
  for the proposed 8 MiB run because long tests are explicitly excluded.

## Decision and Success Gates

1. Adapter: `trySend_drop > 0.1%`, or queue HWM reaches capacity while worker busy is
   above 90%, or adapter expired exceeds 0.5%. Test one static worker/queue size derived
   from observed QPS x p95 RTT. Pass: drops zero, expired down at least 80%, geometric
   mean upload/download up at least 15%, 4 KiB latency no worse than 10%.
2. Resolver/carrier: with adapter drops zero and no queue saturation, DNS expired above
   2% selects a short full-path A/B of the current resolver and one existing candidate.
   Pass: both upload and download arithmetic means improve at least 15%, 4 KiB latency
   no worse than 10%, recovery unchanged, queries/useful MiB not higher.
3. Poll supply: with DNS loss below 2% and adapter drops zero, Busy
   `all_outstanding + 1 < target` in at least 20% of samples selects one local diff that
   counts data and pure-poll DNS queries under existing caps. Pass: download up at least
   15%, mixed up at least 10%, upload no worse than 5%, 4 KiB latency down 15%, timeout
   rise no more than 0.5 percentage points and queries/useful MiB no more than 10%.
4. If Busy underfill is below 5% while bytes-in-transit is at least 90% of cwnd in at
   least 30% of Busy samples, record QUIC/path limitation and stop; broad CC work is
   outside this goal.

## Experiment Log

| Checkpoint | Variant | Download kB/s | Upload kB/s | 4 KiB latency | Result |
|---|---|---:|---:|---:|---|
| Baseline | `bfaf142`, fixed TCP resolver | 91.566 / 85.530 / 127.448; avg 101.515 | 41.940 / 41.940 / 41.940 lower bound; all timed out after sending 2 MiB | 796 ms; 742 ms after cancel | download/recovery pass; upload response timeout |
| Instrumentation control | 32 workers / 64 queue | 132.638 | not repeated; adapter gate already selected | not repeated | 412/3,409 drops (12.09%), HWM 65 |
| Adapter treatment | 32 workers / 128 queue | 136.234 (+2.7%) | not run; mandatory zero-drop gate failed | not run | 797/3,714 drops (21.46%), HWM 129; rejected |

Failures, cancellation/recovery timing and aggregate blocks are recorded beneath the
relevant checkpoint without addresses, QNAMEs or payload data.

## Current Checkpoint

- Closes: R1-R3.
- Smallest next action: commit/push the completed evidence-only goal and stop.
- Expected evidence: clean branch at the pushed documentation checkpoint.
- Stop or replan if: tracked runtime differs from `bfaf142`.

## Current State

- Resolved: R1-R3. The only selected static treatment failed its mandatory gate;
  baseline runtime and build script are restored.
- Last relevant evidence: final non-instrumented APK retained one child/VPN, completed
  an exact 4 KiB payload in 1.133 seconds and stopped to zero child/VPN.
- Blocker: none.
- Next: commit/push and stop.

## Material Decisions

- 2026-08-12: use three exact 2 MiB repetitions rather than 8 MiB or an idle/soak run;
  this gives an average while respecting the explicit no-long-test boundary.
- 2026-08-12: candidate order is evidence-driven: adapter, resolver, poll supply, then
  stop at a measured QUIC/path cap. No speculative combination is allowed.
- 2026-08-12: direct LTE is currently unrestricted: `ya.ru`, Cloudflare
  (`loc=RU`, `warp=off`) and Google were reachable with VPN stopped. Continue only as
  a same-radio forced-tunnel throughput A/B; do not treat it as restricted-LTE proof.
- 2026-08-12: adapter evidence selects queue capacity, not more workers: drops occurred
  with HWM at the 64-slot capacity while workers were busy only 13.6% and expired zero.
  The one tested treatment was a 128-slot queue with 32 workers unchanged.

## Checkpoint History

- 2026-08-12: goal activated from the user diagnosis; baseline behavior is the clean
  pushed `dev` commit `bfaf142`.
- 2026-08-12: R1 passed on unrestricted LTE as a forced-tunnel baseline. Download had
  49% spread; all upload bodies were sent but the Cloudflare endpoint did not complete
  before the bound. Cancellation recovery retained one child and VPN.
- 2026-08-12: R2 passed. One 2 MiB instrumentation run selected adapter saturation:
  412 of 3,409 accepted datagrams dropped (12.09%), queue HWM reached capacity and no
  adapter query expired. The redacted Rust block was captured without traffic metadata.
- 2026-08-12: R3 adapter treatment failed early: queue 128 retained drops (21.46%) and
  yielded only +2.7% in the exact short download. Per the frozen gate, no constants or
  instrumentation are retained and no inapplicable downstream candidate is attempted.

## Completion

- Resolved outcomes: R1 baseline, R2 aggregate attribution and R3 one-candidate A/B are
  verified; no candidate passed its gate, so baseline runtime is retained.
- Commands and artifacts: pinned arm64 Slipstream builds, `:app:assembleOssRelease`,
  exact Cloudflare byte-count/timing probes, redacted adapter/Rust aggregate blocks and
  final installed non-instrumented Android payload/stop acceptance.
- Constraint and diff-scope check: tracked diff is this evidence document only; one
  resolver/child/path, gVisor, authenticated FlowRelay, WARP fail-closed routing and
  quiet scheduling remain unchanged. No server, protocol, dependency or telemetry
  change remains.
- Final status: complete; negative tuning result recorded.
