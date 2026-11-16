# Goal: decide the DNS multipath resolver Pareto point

Status: active
Source: user instruction 2026-11-16 (accepts the multipath RECON plan and asks for iterative
implementation plus test results).
Last updated: 2026-11-16

## Objective
Find out whether more than two DNS resolver paths improve the tunnel enough to justify the extra
TCP/53 lanes and DPI surface. Add per-path aggregate metrics, cap the total lane budget so adding
paths does not double H5, and run a phone A/B of 1, 2 and 4 paths. Keep two paths if the gain is
below the gate; otherwise adopt a capped 4-path (two-provider) preset.

## Execution Directive
Complete the frozen Required Outcomes using the listed Change Envelope and Primary Evidence. Work on
the smallest unresolved outcome. Do not add requirements from reviews, tests, tools, speculative risks,
or optional source text. Finish when every required outcome is resolved and affected constraints remain
satisfied.

## Frozen Contract

### Required Outcomes
- M0: Per-path aggregate metrics.
  - Source: plan step 1.
  - Acceptance: `--dpi-stats` emits one `dpi-path:` line per resolver every 10 s with resolver address,
    qps, send bytes/packets and dns responses deltas; no QNAMEs or payloads.
  - Primary evidence: local and phone logcat lines for 2 and 4 paths.
  - Status: done.
  - Evidence: `dpi-path:` lines on the phone show `77.88.8.88:53` and `77.88.8.1:53`, each about
    296/277 qps and 174/170 KB per 10 s during a Speedtest; the 4-path attempt shows
    `176.59.127.174:53` receiving probe traffic while its path is not added. No QNAME/payload is logged.
- M1: Total lane budget.
  - Source: plan step 2.
  - Acceptance: the app passes `min(112, 224 / tcp_paths)` lanes and a bounded total queue, so 2 paths
    keep 112 lanes each and 4 paths use 56 each; total active lanes never exceed 224.
  - Primary evidence: `dpi-stats` lanes on 2 and 4 paths; phone Speedtest.
  - Status: done.
  - Evidence: app passes `min(112, 224/tcp_paths)` and `min(128, 256/tcp_paths)`. Phone 1 path:
    `lanes=112`; 2 paths: 112 each, active 168; automatic single resolver: 60; manual 4-path bridge:
    32 idle. Total lanes stay within 224 by construction.
- M2: Phone A/B and Pareto decision.
  - Source: plan step 3.
  - Acceptance: 1 path (`tcp://77.88.8.88:53`), 2 paths (`tcp+mp`, Yandex .88/.1) and 4 paths
    (automatic: Tele2 .174/.175 + Yandex .8/.1) each get at least one Speedtest and per-path stats.
    Adopt 4 paths only if DL or UL improves >=10% or failures drop materially while total lanes stay
    <=224 and H5 does not grow; otherwise keep 2 as the Pareto optimum.
  - Primary evidence: Speedtest DL/UL/ping, qps, lanes, per-path traffic share, H flags.
  - Status: done; decision: keep 2 paths.
  - Evidence: controlled manual sidecar 10 MB Cloudflare download:
    * 1 path `77.88.8.88`: 313 kB/s (31.9 s);
    * 2 paths `.88/.1`: 477/487 kB/s (20.9/20.5 s) - about 52-55 % faster;
    * 3 Yandex paths `.88/.1/.8`: only 2 paths established, 478/488 kB/s - no gain;
    * 4 paths `.88/.1` + Tele2 `.174/.175`: only 2 paths established, 374 kB/s; the third path
      fails with `Failed adding path`, `.174` carries probe traffic only.
    App Speedtest in the same window: 1 path DL 4.86/5.29, UL 0.45/0.48; 2 paths DL 4.85/4.67,
    UL 0.45/0.42; automatic single Tele2 path DL 2.73, UL 0.22; earlier 2-path run DL 7.86, UL 0.64.
    The deployed client/runtime establishes at most two paths even with a third Yandex resolver; the
    server advertises 5, so this is a client/two-path-runtime limit, not a resolver capacity result.
    Adding a third/fourth resolver is a regression (failed probes, extra lanes) and the operator
    resolver is slower. The current two Yandex paths remain the Pareto choice.

### Constraints
- C1: Keep the existing tunnel contracts and the current 2-path profile as a fallback.
- C2: No new dependency; existing client/app code only.
- C3: No QNAME/payload/token logs; only aggregate per-path counters.
- C4: Server supports at most 5 paths (`initial_max_path_id=4`); do not change that here.
- C5: DPI goal: total lanes/qps must not exceed the current 2-path envelope when adding paths.

### Non-goals
- 5+ paths, server picoquic changes, resolver-list UI redesign, Phase 2 base64 TXT, cover traffic.

## Change Envelope
- Client: `slipstream-rust/crates/slipstream-client` (`stats.rs`, runtime path reporting).
- App: `owenclave/.../SlipstreamInstance.kt` (lane/queue budget).
- Tests: local Rust/C, phone Speedtest and `dpi-stats` logcat.
- Forbidden: QNAME/payload logs, new public endpoints, 6+ paths.

## Current Checkpoint
- Closes: M0, M1, M2.
- Smallest next action: none for resolver count. If >2 paths ever become a requirement, raise the
  client picoquic `initial_max_path_id`/path limit and add server path coverage as a separate project.
- Expected evidence: two-path baseline and the failed third-path attempts are recorded.
- Stop or replan if: three paths can be established end-to-end and improve DL/UL >=10 % within the
  224-lane envelope; that would justify a new profile list and a new test.

## Current State
- Resolved: M0 per-path metrics; M1 224-lane budget; M2 decision to keep 2 paths.
- Last relevant evidence: 2 paths ~477-487 kB/s vs 1 path 313 kB/s on the same 10 MB download;
  3 Yandex paths establish only 2; 4 paths establish only 2; automatic single Tele2 is much slower.
- Blocker: third-path establishment; root cause is the client/two-path runtime, not a lack of DNS
  providers.
- Next: no further paths; observation only.

## Material Decisions
- 2026-11-16: two IPs of the same Yandex provider are not two independent paths; the Pareto candidate
  is two providers, not more Yandex IPs.
- 2026-11-16: adding paths without a total lane cap is a DPI regression by construction, so the cap is
  part of the measurement, not an optimization.
- 2026-11-16: the current client/runtime establishes at most two QUIC paths; a third resolver fails
  even when it is another Yandex IP. The deployed server advertises five, so any >2 work needs client
  picoquic/path coverage first. The practical Pareto answer is therefore two paths, not four.

## Checkpoint History
- 2026-11-16: goal created from the accepted multipath plan.
- 2026-11-16: per-path stats and the 224-lane budget implemented; phone A/B shows 2 paths > 1 path,
  while 3/4 paths cannot be established; decision: keep 2.
