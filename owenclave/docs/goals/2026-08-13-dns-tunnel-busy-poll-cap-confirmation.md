# Goal: Confirm the DNS Tunnel Busy poll cap without order bias

Status: complete
Source: user-approved Busy poll-cap follow-up plan, 2026-08-13
Last updated: 2026-08-13

## Objective

Repeat the cap-eight experiment in four adjacent order-balanced control/treatment
blocks on the ADB device, retain the cap only if the original all-gates contract passes,
and otherwise restore the pinned baseline before selecting any further tuning work.

## Execution Directive

Complete the frozen Required Outcomes using the listed Change Envelope and Primary
Evidence. Work on the smallest unresolved outcome. Do not add requirements from
reviews, tests, tools, speculative risks or optional source text. Finish when every
required outcome is resolved and affected constraints remain satisfied.

## Frozen Contract

### Required Outcomes

- R1: Build behavior-matched control and cap-eight artifacts with temporary aggregates.
  - Source: approved order-balanced confirmation plan.
  - Acceptance: both APKs use the same pinned source and aggregate-only instrumentation;
    treatment differs only by capping the additional authoritative Busy poll-path burst
    at eight. Counters report ordinary/poll sends, responses, QUIC loss, adapter
    accepted/completed/full-drop, cap suppression and maximum combined turn burst.
  - Primary evidence: focused source diff, pinned arm64 builds and APK native hashes.
  - Status: verified
  - Evidence: pinned arm64 control and treatment builds succeeded. Both use identical
    Kotlin/Rust aggregate instrumentation; treatment adds only the Busy poll cap.
    Control native/APK SHA-256 are `935ea6dc...` / `2d3bbff9...`; treatment native/APK
    SHA-256 are `bf32d9fd...` / `6e114fe9...`, and each APK packages its matching native
    library.

- R2: Complete four order-balanced adjacent A/B blocks on the production gVisor path.
  - Source: approved `C→T / T→C / C→T / T→C` plan.
  - Acceptance: eight fresh-child windows run in the frozen order with the same strict
    TCP resolver, LTE radio/position and endpoint. Each window records one unscored
    128 KiB warm-up, exact 1 MiB download, acknowledged 512 KiB upload, exact 4 KiB
    probe and cancellation/recovery result; failures are not replaced.
  - Primary evidence: exact status/bytes/timings and aligned per-window aggregate deltas.
  - Status: verified
  - Evidence: after one rejected harness preflight caused by shell quoting, the fixed
    runner completed all eight frozen windows without replacement. Download kB/s were
    C1/T1 `155.029/135.539`, T2/C2 `135.384/130.230`, C3/T3
    `112.570/115.708` and T4/C4 `146.932/145.582`. Upload kB/s were
    `14.633/16.064`, `15.310/16.139`, `16.436/12.371` and `14.203/12.701` in
    window order; every POST returned HTTP 200. All exact 4 KiB probes and cancellation
    recovery passed with one child/VPN, `loc=DE`, `warp=on`.

- R3: Apply the original gates and retain only a reproducible winner.
  - Source: user-approved evidence-driven decision plan.
  - Acceptance: retain cap eight only if the geometric mean of four paired download
    ratios is at least +15%, both order strata are non-regressive, every treatment
    window has adapter drops at most 0.1%, pooled QUIC loss/useful MiB is at most 50%
    of control, acknowledged upload is no worse than 5%, 4 KiB latency is no worse than
    10%, recovery retains one child/VPN and quiet scheduler source remains unchanged.
    Otherwise restore the pinned baseline and stop this treatment.
  - Primary evidence: calculation from R2 plus final installed-release payload, egress,
    Stop and stable DE service restart counters.
  - Status: verified
  - Evidence: paired download ratios were 0.8743, 1.0396, 1.0279 and 1.0093;
    geometric mean was 0.9854 (-1.46%), far below +15%. The C→T order stratum was
    0.9480 and T→C was 1.0243, confirming order dependence. Treatment upload geometric
    ratio was 0.9676 (-3.24%) and latency ratio 0.9282 (-7.18%), both within bounds.
    Treatment full-drop rates were 0.118%, 0.208%, 0% and 0%; the first two failed the
    per-window 0.1% gate. Pooled scored QUIC loss was 40 versus control 60 (66.7%, not
    the required at most 50%). The cap was rejected and all instrumentation removed.
    Rebuilt normal native SHA-256 was `785f4de...`, matching the APK. Final installed
    release returned exact 4 KiB HTTP 200 in 647 ms with DE/WARP egress, emitted zero
    aggregate logs and stopped to zero child/VPN. DE services stayed active with restart
    counters unchanged at 5 and 0.

### Constraints

- C1: Keep one strict TCP resolver, one child/path, gVisor capture, authenticated
  FlowRelay and fail-closed WARP egress.
- C2: Instrumentation is temporary, in-memory and aggregate-only. Never print or retain
  addresses, QNAMEs, DNS IDs, payloads, tokens, credentials or complete profiles.
- C3: Do not change adapter constants, ordinary packet generation, pacing/cwnd,
  Warm/Quiescent/Empty polling, keepalive, congestion control, server or resolver.
- C4: Each window stays below nine minutes; no 30-60 minute soak.
- C5: Historical cap results are context only and excluded from the primary estimate.

### Non-goals

- Shared total cap, full outstanding accounting, cap sweep, queue/worker tuning,
  resolver/transport work, multipath or server changes.
- Treating unrestricted LTE as Restricted LTE acceptance.
- Keeping diagnostic telemetry in the final runtime.

## Change Envelope

- Target: temporary counters at the pinned Rust authoritative send path and Android TCP
  adapter, plus one treatment-only Busy poll cap of eight.
- Expected paths: `SlipstreamInstance.kt`, `bin/lib/slipstream/build.sh`, temporary
  local patches over pinned `runtime.rs`, `dns/{poll,response}.rs` and `resolver.rs`,
  and this goal.
- Allowed artifacts: two temporary APKs, aggregate log output and ignored native build
  products. A static cap patch may remain only after R3 passes.
- Forbidden artifacts: dependencies, protocol/schema changes, persistent logs, traffic
  metadata, services, server changes or extra runtime paths.

## Measurement and Decision

- Frozen order: `C1→T1`, `T2→C2`, `C3→T3`, `T4→C4`.
- Fresh child for every window; verify one child/VPN and DE/WARP egress.
- Primary download effect: geometric mean of `T1/C1`, `T2/C2`, `T3/C3`, `T4/C4`.
- Order strata: geometric means of `T1/C1,T3/C3` and `T2/C2,T4/C4`.
- Pool exact useful bytes for drop/loss and use geometric means for upload and latency.
- Any wrong bytes/status, radio change, child restart or wrong egress invalidates the
  affected block rather than silently replacing one observation.

## Current Checkpoint

- Closes: R1-R3.
- Smallest next action: stop this tuning line; a new approved goal is required for any
  different mechanism.
- Expected evidence: baseline runtime retained and no diagnostic artifacts tracked.
- Stop or replan if: none; this confirmation is terminal.

## Current State

- Resolved: R1-R3. Balanced confirmation rejects cap eight: no throughput gain, one
  order stratum regressed and drop/loss gates also failed.
- Last relevant evidence: rebuilt normal release passed exact payload, DE/WARP egress,
  zero-aggregate-log and Stop acceptance.
- Blocker: none.
- Next: stop.

## Material Decisions

- 2026-08-13: use four blocks so both treatment orders are replicated twice.
- 2026-08-13: keep the original gates; improved counters cannot waive the throughput
  finish line.

## Checkpoint History

- 2026-08-13: goal activated from the approved post-RECON confirmation plan.
- 2026-08-13: R1 verified; both temporary APK variants built from the pinned source.
- 2026-08-13: the first C1 attempt was excluded before scoring because an ADB shell
  quoting error split curl format strings; the corrected runner then executed the full
  precommitted order from the beginning.
- 2026-08-13: R2-R3 verified. Balanced cap-eight effect was -1.46% with opposing order
  strata; treatment failed per-window drop and pooled-loss gates and was removed.

## Completion

- Resolved outcomes: R1 matched artifacts, R2 eight valid order-balanced windows and R3
  all-gates decision are verified; cap eight is rejected.
- Commands and artifacts: pinned arm64/APK builds, ADB installs, exact Termux curl
  payloads, aligned aggregate snapshots, geometric-ratio calculations, final native/APK
  hash match and Android/DE runtime acceptance.
- Constraint and diff-scope check: cap, temporary counters, patches, runner and build
  entries are absent; no runtime, resolver, scheduler or server change remains.
- Final status: complete; order-balanced evidence does not support further cap-eight
  work.
