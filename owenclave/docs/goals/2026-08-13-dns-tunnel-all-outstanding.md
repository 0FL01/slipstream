# Goal: Test all-DNS outstanding-aware Busy polling

Status: complete
Source: user-approved all-DNS outstanding plan, 2026-08-13
Last updated: 2026-08-13

## Objective

Measure whether unresolved ordinary and poll-path DNS transactions exceed the Busy
pacing target, then test one treatment that creates additional Busy polls only from
`target - all DNS outstanding`. Retain it only if short order-balanced Android evidence
passes every throughput, loss, latency and recovery gate.

## Execution Directive

Complete the frozen Required Outcomes using the listed Change Envelope and Primary
Evidence. Work on the smallest unresolved outcome. Do not add requirements from
reviews, tests, tools, speculative risks or optional source text. Finish when every
required outcome is resolved and affected constraints remain satisfied.

## Frozen Contract

### Required Outcomes

- R1: Establish synchronized control evidence for real DNS outstanding pressure.
  - Source: approved requirement to confirm the premise before changing behavior.
  - Acceptance: a behavior-neutral aggregate-only build records two fresh-child control
    windows. Each aligned workload interval reports ordinary/poll sends, matched
    responses, expiry, current/maximum all-DNS outstanding, Busy target/outstanding
    sums and over-target samples, QUIC loss, plus adapter accepted/completed/full/closed
    drops, failures, expiry, queue and active-worker high-water marks.
  - Primary evidence: two exact 1 MiB downloads with synchronized pre/post snapshots.
  - Status: verified
  - Evidence: two fresh-child exact 1 MiB controls completed at 121.050 and
    133.699 kB/s with one child/VPN and DE/WARP egress. C1 recorded 3,373 Busy samples,
    2,857 above target (84.7%), average target 29.9, average outstanding 60.1 and HWM
    416; adapter/Rust both recorded 320 full-drop/expiry events. C2 recorded 3,144 Busy
    samples, 3,058 above target (97.3%), average target 27.5, average outstanding 62.5
    and HWM 434; adapter/Rust both recorded 338 full-drop/expiry events. Both windows
    therefore independently pass the premise gate with aligned lifetime boundaries.

- R2: Test one all-DNS outstanding-aware treatment when R1 confirms its premise.
  - Source: approved hypothesis.
  - Acceptance: if both control windows spend at least 50% of Busy samples above target
    and average outstanding exceeds average target, build a treatment that tracks every
    successfully sent authoritative DNS ID until matching response or five-second
    expiry. In Busy only, additional poll deficit becomes
    `target - all_dns_outstanding`; ordinary sends and existing pacing/cwnd/burst caps
    remain unchanged. Otherwise mark this outcome not applicable and restore baseline.
  - Primary evidence: focused source diff, pinned arm64 builds and four order-balanced
    `C→T / T→C / C→T / T→C` blocks on the same strict TCP resolver and LTE path.
  - Status: verified
  - Evidence: premise passed, so matched pinned control/treatment APKs were built with
    identical aggregate bookkeeping; treatment changed only additional Busy poll
    deficit from QUIC in-flight estimate to all-DNS outstanding. Four blocks completed
    in frozen order. Control/treatment download kB/s were `142.746/124.916`,
    `155.796/118.718`, `137.802/92.732` and `163.562/104.837`. Every exact download,
    acknowledged upload, probe and recovery returned HTTP 200 with one child/VPN and
    DE/WARP egress.

- R3: Retain only a treatment that passes every frozen gate.
  - Source: user-approved evidence-driven decision rule.
  - Acceptance: retain treatment only if paired download geometric mean improves at
    least 15% with at least three of four positive pairs; every treatment window has
    adapter full drops at most 0.1% and DNS expiry below 2%; queries per useful MiB
    decrease; average Busy outstanding is no more than 1.25 times target and materially
    lower than control; acknowledged upload is no worse than 5%; exact 4 KiB latency is
    no worse than 10%; recovery keeps one child/VPN; quiet scheduler source is unchanged.
    Otherwise restore the pinned baseline.
  - Primary evidence: R2 calculations and final installed-release payload, DE/WARP
    egress, Stop and stable DE restart counters.
  - Status: verified
  - Evidence: treatment failed decisively and was removed. All four download pairs were
    negative; paired geometric ratio was 0.732 (`-26.8%`). Upload ratio was 0.958
    (`-4.2%`, within bound), 4 KiB latency ratio 0.868, and recovery ratio 1.050. During
    scored intervals treatment reduced pooled expiry from 1,313/26,988 (4.87%) to zero,
    reduced Busy over-target samples from 28.3% to 3.5%, and reduced average
    outstanding/target from 0.876 to 0.528. Adapter drops were zero in both arms after
    warm-up, while QUIC loss was 115 control versus 137 treatment. The accounting
    successfully removed excess work but under-supplied useful downstream traffic; it
    missed throughput, positive-pair and outstanding-near-target gates.

### Constraints

- C1: Keep one strict TCP resolver, one child/path, gVisor capture, authenticated
  FlowRelay and fail-closed WARP egress.
- C2: Temporary instrumentation is in-memory, aggregate-only and removed after the
  decision. Never print or retain addresses, QNAMEs, DNS IDs, payloads, credentials,
  tokens or complete profiles.
- C3: Preserve ordinary DNS generation, pacing/cwnd target, burst caps, congestion
  control, adapter constants, Warm/Quiescent/Empty polling and all keepalive behavior.
- C4: Each runtime window stays below nine minutes; no 30-60 minute soak.
- C5: DNS Tunnel downtime is acceptable; server, FlowRelay, WARP, listener, firewall
  and resolver configuration remain unchanged.

### Non-goals

- Shared per-turn caps, cap sweep, queue/worker/retry/timeout tuning, resolver changes,
  local failure responses, pipelining, multipath, BBR/DCubic or upstream upgrades.
- Treating unrestricted LTE as Restricted LTE acceptance.
- Keeping diagnostic telemetry in the final runtime.

## Change Envelope

- Target: pinned authoritative DNS transaction bookkeeping and additional Busy poll
  deficit, with temporary Android adapter aggregates.
- Expected paths: `SlipstreamInstance.kt`, `bin/lib/slipstream/build.sh`, temporary
  patches over pinned `runtime.rs` and `dns/{resolver,poll,response,debug}.rs`, this goal,
  and stable `docs/dns-tunnel.md` only if treatment remains.
- Direct consumers: generated ignored arm64 `libslipstream.so`, release APK,
  `V2RayInstance` and the installed DNS Tunnel profile.
- Allowed artifacts: temporary aggregate patches, two temporary APK variants and a
  temporary ADB/Termux runner outside tracked source. One bookkeeping patch may remain
  only after R3 passes.
- Forbidden artifacts: dependencies, protocol/schema changes, persistent telemetry,
  services, traffic metadata, server changes or another runtime path.

## Measurement and Decision

- R1 uses two fresh control children and one exact 1 MiB download per aligned interval.
- R2 order is `C1→T1`, `T2→C2`, `C3→T3`, `T4→C4`, with fresh child per window, fixed
  radio/position/resolver/endpoint and exact failures retained without replacement.
- Each scored R2 window uses an unscored 128 KiB warm-up, exact 1 MiB download, exact
  512 KiB POST requiring final HTTP 2xx, exact 4 KiB probe, bulk cancellation and exact
  4 KiB recovery probe.
- Use adjacent cumulative snapshots around each scored workload. Normalize sends,
  expiry, drops and QUIC loss by exact useful bytes. Use geometric means for paired
  speed, upload, latency and recovery ratios.
- Wrong bytes/status, changed radio/underlay, child restart or wrong egress invalidates
  the affected block rather than silently replacing an inconvenient result.

## Current Checkpoint

- Closes: R1-R3.
- Smallest next action: stop; a new approved mechanism is required for more tuning.
- Expected evidence: baseline runtime retained with no temporary artifacts.
- Stop or replan if: none; this experiment is terminal.

## Current State

- Resolved: R1-R3. Overproduction is real, but directly subtracting every unresolved DNS
  transaction overcorrects and cuts download throughput by 26.8%.
- Last relevant evidence: rebuilt normal native SHA-256 `785f4de1...` matches the final
  APK. The installed release returned exact 4 KiB HTTP 200 in 884 ms with DE/WARP
  egress, emitted zero diagnostic aggregates and stopped to zero child/VPN. DE services
  stayed active with restart counters unchanged at 5 and 0.
- Blocker: none.
- Next: stop.

## Material Decisions

- 2026-08-13: require two synchronized control windows before behavior changes; old
  aggregate totals are context only.
- 2026-08-13: bookkeeping controls only additional Busy polls. It does not throttle
  ordinary prepared QUIC packets or change quiet phases.

## Checkpoint History

- 2026-08-13: goal activated from the approved all-outstanding hypothesis.
- 2026-08-13: R1 verified; both aligned controls exceeded the 50% over-target gate and
  reproduced matching local full-drop and Rust expiry counts.
- 2026-08-13: R2-R3 verified. Full all-DNS subtraction removed expiry but starved
  downstream supply; all four download pairs regressed and the treatment was removed.
- 2026-08-13: normal pinned release rebuilt and passed payload, egress, no-telemetry and
  Stop acceptance with stable DE service counters.

## Completion

- Resolved outcomes: R1 synchronized premise, R2 four paired treatment blocks and R3
  all-gates decision are verified; all-DNS subtraction is rejected.
- Commands and artifacts: pinned arm64/APK builds, eight order-balanced ADB windows,
  exact Termux HTTP payloads, synchronized aggregate snapshots, geometric-ratio
  calculations, final native/APK hash match and Android/DE runtime acceptance.
- Constraint and diff-scope check: bookkeeping treatment, instrumentation patches,
  Android counters, diagnostic flag, raw result files and temporary runner are absent;
  no runtime, resolver, adapter, server or quiet scheduler change remains.
- Final status: complete; overproduction is confirmed, but full outstanding subtraction
  is a throughput dead end.
