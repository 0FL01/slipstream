# Goal: Evaluate the DNS Tunnel Busy poll cap for stability

Status: complete
Source: user-approved stability follow-up, 2026-08-13
Last updated: 2026-08-13

## Objective

Compare the pinned runtime with the Busy poll-path cap of eight in short,
order-balanced Android windows, then retain the cap only if it reproducibly improves
HTTP tail stability without reducing median download speed by more than 5%.

## Execution Directive

Complete the frozen Required Outcomes using the listed Change Envelope and Primary
Evidence. Work on the smallest unresolved outcome. Do not add requirements from
reviews, tests, tools, speculative risks or optional source text. Finish when every
required outcome is resolved and affected constraints remain satisfied.

## Frozen Contract

### Required Outcomes

- R1: Build behavior-matched control and cap-eight release artifacts.
  - Source: approved stability comparison.
  - Acceptance: both APKs use the same pinned source and normal release behavior;
    treatment differs only by limiting the additional authoritative Busy poll-path
    burst to eight. No diagnostic telemetry is added.
  - Primary evidence: focused source diff, successful pinned arm64/APK builds and
    matching native hashes inside each APK.
  - Status: verified
  - Evidence: the normal control native/APK hashes are `785f4de1...` / `6224894e...`.
    The pinned treatment build succeeded with only `BUSY_POLL_BURST_MAX = 8` and its
    one Busy-path `min`; treatment native/APK hashes are `fbd8737d...` / `1fa1b5d1...`.
    Each APK packages the native library matching its recorded variant.

- R2: Complete four short order-balanced stability blocks on the gVisor path.
  - Source: user instruction to compare tail latency, HTTP failures, recovery and speed
    variance without a long soak.
  - Acceptance: run `C1→T1`, `T2→C2`, `C3→T3`, `T4→C4`, with a fresh child in every
    window and the same strict TCP resolver, LTE radio/position and endpoint. Record two
    exact 1 MiB download speeds, then twenty exact 4 KiB probes while four 1 MiB bulk
    downloads are active, cancel bulk and record one exact 4 KiB recovery probe.
    Wrong status/bytes and timeouts remain failures and are not replaced.
  - Primary evidence: per-window status, exact bytes, monotonic timings, child/VPN and
    DE/WARP observations.
  - Status: verified
  - Evidence: all eight valid windows completed in frozen order with fresh children,
    one VPN, LTE and `loc=DE`, `warp=on`. Every one of 16 bulk downloads, 160 loaded
    probes and eight recovery probes returned HTTP 200 with exact bytes; failures and
    timeouts were zero in both variants. Control/treatment window download geometric
    means were `128.442/121.004`, `129.062/137.274`, `114.354/153.108` and
    `154.072/140.276` kB/s. Loaded-probe p95 values were `974/762`, `792/830`,
    `1052/804` and `835/965` ms; recovery was `673/493`, `513/499`, `510/515` and
    `549/607` ms.

- R3: Retain only a stability winner and restore a normal final release.
  - Source: approved retention rule.
  - Acceptance: retain cap eight only when treatment has no more HTTP failures than
    control, has fewer failures when control has any, improves pooled loaded-probe p95
    by at least 10%, is non-regressive in at least three of four paired p95 results, and
    keeps both paired geometric-mean and pooled-median download speed at least 95% of
    control. Every recovery must pass; treatment recovery geometric mean must be no
    worse than 10%. Otherwise restore the pinned baseline. In either case the final
    release retains one child/VPN, exact payload, DE/WARP egress and clean Stop.
  - Primary evidence: calculation from R2, final source/build diff and installed-release
    acceptance.
  - Status: verified
  - Evidence: cap eight failed the stability gate and was removed. Pooled loaded p95
    improved only 1.1% (`896` to `886` ms), below 10%, and paired p95 improved in only
    two of four blocks; paired p95 geometric ratio was 0.948. There were no failures in
    either arm to reduce. Speed passed the guard: paired geometric ratio was 1.051 and
    pooled median rose from 129.062 to 138.709 kB/s; sample CV fell from 10.9% to 8.5%.
    Recovery geometric ratio was 0.945 and all recoveries passed. The rebuilt baseline
    native SHA-256 `785f4de1...` matches the final APK, which returned exact 4 KiB HTTP
    200 in 956 ms with DE/WARP egress and stopped to zero child/VPN. DE services stayed
    active with restart counters unchanged at 5 and 0.

### Constraints

- C1: Keep one strict TCP resolver, one child/path, gVisor capture, authenticated
  FlowRelay and fail-closed WARP egress.
- C2: Do not add traffic logging or record addresses, QNAMEs, DNS IDs, payloads, tokens,
  credentials or complete profiles.
- C3: Change only the additional authoritative Busy poll-path count. Preserve ordinary
  generation, pacing/cwnd, adapter behavior, congestion control, Warm/Quiescent/Empty
  polling and keepalive.
- C4: Each window stays below four minutes and the complete runtime measurement stays
  below 20 minutes; no 30-60 minute soak.
- C5: Prior throughput-cap data is contextual only and does not count toward R2.

### Non-goals

- Proving a throughput gain, all-DNS outstanding accounting, shared burst caps, cap
  sweeps, queue/worker tuning, resolver changes, multipath or server changes.
- Treating unrestricted LTE as Restricted LTE acceptance.
- Adding instrumentation to explain a failed stability result.

## Change Envelope

- Target: pinned Slipstream authoritative Busy poll-path burst.
- Expected paths: `bin/lib/slipstream/build.sh`, one temporary cap patch over pinned
  `runtime.rs`, stable `docs/dns-tunnel.md` only if the cap is retained, and this goal.
- Direct consumers: generated ignored arm64 `libslipstream.so`, release APK,
  `V2RayInstance` and the installed DNS Tunnel profile.
- Allowed artifacts: two temporary APKs and a temporary ADB/Termux runner outside the
  tracked source. The static cap patch may remain only after R3 passes.
- Forbidden artifacts: dependencies, protocol/schema changes, telemetry, services,
  server changes or extra runtime paths.

## Measurement and Decision

- Frozen order: `C1→T1`, `T2→C2`, `C3→T3`, `T4→C4`.
- Use a fresh child per window. Verify one child/VPN and DE/WARP egress before scoring.
- Each exact request uses a cache-busting nonce, HTTP 200 and a hard timeout. Record a
  failed request once; do not retry it into a success.
- Window speed is the geometric mean of its two exact 1 MiB downloads. Report pooled
  median and coefficient of variation for each variant.
- Window tail is the nearest-rank p95 of twenty loaded 4 KiB probes, with timeouts
  treated as failures rather than fabricated latency values. Report pooled p95 over all
  successful probes and failure counts separately.
- Pair by block. Primary ratios are geometric means of four treatment/control window
  ratios for speed, loaded p95 and recovery.
- Stop early only for wrong egress, child restart, radio/underlay change or a functional
  failure that already makes the retention contract impossible.

## Current Checkpoint

- Closes: R1-R3.
- Smallest next action: stop; a new approved mechanism is required for further tuning.
- Expected evidence: baseline runtime retained with no temporary artifacts.
- Stop or replan if: none; this experiment is terminal.

## Current State

- Resolved: R1-R3. Cap eight did not reproducibly improve application-visible tail
  stability and is rejected despite better speed variance in this short sample.
- Last relevant evidence: normal pinned release passed payload, egress and Stop with
  stable DE restart counters.
- Blocker: none.
- Next: stop.

## Material Decisions

- 2026-08-13: stability is a separate finish line; the previous +15% throughput gate
  remains failed and is not rewritten.
- 2026-08-13: use application-visible HTTP tails and recovery without runtime telemetry;
  the cap already has sufficient mechanism evidence for this bounded confirmation.

## Checkpoint History

- 2026-08-13: goal activated from the approved stability follow-up.
- 2026-08-13: R1 verified; pinned control and cap-eight release artifacts built and
  matched to their packaged native libraries.
- 2026-08-13: R2 verified; eight frozen windows completed with no HTTP failures.
- 2026-08-13: R3 verified; pooled p95 improvement was 1.1% and only two of four pairs
  improved, so the cap was removed and the normal release restored.

## Completion

- Resolved outcomes: R1 matched artifacts, R2 eight valid stability windows and R3
  retention decision are verified; cap eight is rejected.
- Commands and artifacts: pinned arm64/APK builds, ADB installs, exact Termux HTTP
  payloads, paired geometric calculations, final native/APK hash match and Android/DE
  runtime acceptance.
- Constraint and diff-scope check: cap patch, build entry, temporary runner, APK copies
  and raw result files are absent; no runtime, resolver, adapter, server or quiet
  scheduler change remains.
- Final status: complete; the bounded stability evidence does not justify retaining the
  cap.
