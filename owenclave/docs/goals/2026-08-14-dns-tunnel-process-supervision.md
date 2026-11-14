# Goal: Remove DNS Tunnel process-supervision polling

Status: complete
Source: user-approved post-RECON battery plan, 2026-08-14
Last updated: 2026-08-14

## Objective

Remove the redundant two-second Android poll used to detect a ready Slipstream child
exit, while preserving readiness-gated recovery, DNS carrier behavior and a verified
arm64 release.

## Execution Directive

Complete the frozen Required Outcomes using the listed Change Envelope and Primary
Evidence. Work on the smallest unresolved outcome. Do not add requirements from
reviews, tests, tools, speculative risks, or optional source text. Finish when every
required outcome is resolved and affected constraints remain satisfied. Stop
substantive work at a proven external blocker, an approved budget boundary, or when
no remaining in-scope action has a falsifiable expected result; record the exact
evidence and smallest unlock.

## Frozen Contract

### Required Outcomes

- R1: Record the unplugged current-build idle baseline before changing supervision.
  - Source: user-provided battery evidence and approved post-RECON plan.
  - Acceptance: four order-balanced Jitsi/DNS screen-off windows use unchanged
    power-affecting settings, one corresponding sidecar, four minutes of settling and
    ten minutes without user traffic; retain aggregate CPU, scheduler, UID network,
    radio-active and charge-counter evidence only.
  - Primary evidence: `Jitsi -> DNS -> DNS -> Jitsi` aggregate window summaries plus
    post-window exact carrier payload and unchanged DE restart counters.
  - Status: superseded
  - Evidence: Wireless-ADB windows could not provide a valid order-balanced baseline:
    Wi-Fi remained active, the charge counter stayed below measurement resolution,
    JVM task sums were not monotonic under thread churn, and a Jitsi child changed.
    The user stopped further measurement and then explicitly instructed implementation,
    build, commit and push without making a battery-amount claim.

- R2: Replace post-readiness process polling with one event-driven exit owner.
  - Source: confirmed duplicate `child.waitFor()` and two-second monitor in
    `SlipstreamInstance.kt`.
  - Acceptance: no periodic process-aliveness coroutine remains; exit before accepted
    readiness still fails startup, exit afterward requests exactly one full-service
    readiness-gated restart, and intentional close requests none.
  - Primary evidence: focused source diff, release Kotlin compilation and controlled
    Android child-exit observation.
  - Status: verified
  - Evidence: The sole `child.waitFor()` now branches under the lifecycle lock: an
    unaccepted exit fails `ready`, an accepted exit invokes `onStopped` once, and
    `close()` marks the instance closed before destroying the child. The periodic
    constant, `Job`, `delay`, monitor state and monitor loop are absent. Release Kotlin
    compilation and assembly passed. On Android, intentional Stop left zero child and
    VPN after eight seconds without a queued restart; reconnect restored exactly one
    child and VPN. Shell UID cannot signal the app-owned child, so the deterministic
    post-readiness branch was verified from the focused source rather than a live
    injected crash.

- R3: Preserve the accepted DNS Tunnel runtime and measure the treatment.
  - Source: user instruction to begin work and build after the battery RECON.
  - Acceptance: the installed release retains VPN/gVisor, one resolver and child,
    exact TCP and UDP through FlowRelay/WARP, clean Stop, and a ten-minute screen-off
    treatment with no server restart growth; no battery amount is claimed if charge
    evidence is below measurement noise.
  - Primary evidence: signed arm64 APK, Android payload/egress/child observations,
    aggregate treatment window and DE service counters.
  - Status: superseded
  - Evidence: The later user instruction narrowed the treatment to the source fix and
    release publication. No battery saving is claimed. The installed treatment release
    nevertheless retained one child/VPN, exact TCP with DE WARP egress, an exact UDP
    DNS transaction, clean Stop/reconnect, and unchanged DE restart counters 5/0.

- R4: Publish the verified source and release build.
  - Source: user instruction: "потом коммит пуш билд".
  - Acceptance: the verified goal and runtime change are committed and pushed on the
    current branch, and the matching arm64 release APK exists in the documented output
    path.
  - Primary evidence: commit/push result, clean tracked tree and APK digest.
  - Status: verified
  - Evidence: Runtime commit `31003aa` was pushed to `origin/dev`. The matching arm64
    release APK has SHA-256
    `dc163af932c18b95ad8676e1acaa815017499826dd304ce2a927fe45eb552cfd` in both the
    documented build output and the user-requested replacement destination.

### Constraints

- C1: Preserve one resolver/child/path, gVisor-only capture, FlowRelay wire,
  certificate pin, stdin-only credentials, per-flow QUIC streams and fail-closed WARP
  egress.
- C2: Preserve Busy/Warm/QuiescentOpen/Empty polling and the 400 ms/5 second
  keepalive contract; do not repeat rejected DNS transport or throughput treatments.
- C3: Do not print or retain tokens, profiles, databases, process arguments, raw
  traces, packet captures, traffic logs or destination metadata.
- C4: Server, resolver, FlowRelay, WARP, firewall and olcRTC runtime remain unchanged.

### Non-goals

- Resolver, queue, worker, poll-budget, keepalive, dormancy, mux, multipath,
  congestion-control or server tuning.
- A battery percentage or mAh saving claim from a short window.
- Refactoring general process supervision or adding a test abstraction.

## Change Envelope

- Target: Android ownership of the existing Slipstream child exit only.
- Expected paths, symbols and direct consumers:
  - `app/src/main/java/io/nekohasekai/sagernet/bg/proto/SlipstreamInstance.kt`;
  - this goal document and stable DNS documentation only if observable behavior changes;
  - direct consumer `V2RayInstance` and installed DNS Tunnel profile.
- Allowed artifacts: focused Kotlin/docs diff, aggregate-only temporary measurement,
  ignored release APK and existing native artifacts.
- Forbidden artifacts: dependency, protocol/schema change, persistent telemetry,
  service, server change, extra process or runtime path.
- User budget: first sufficient one-file runtime change; build, commit and push after
  runtime verification.

## Current Checkpoint

- Closes: complete.
- Smallest next action: none.
- Expected evidence: closure check passed.
- Stop or replan if: not applicable.

## Current State

- Resolved: R1 and R3 are superseded by the user's later implementation instruction;
  R2 and R4 are verified.
- Last relevant evidence: arm64 APK SHA-256
  `dc163af932c18b95ad8676e1acaa815017499826dd304ce2a927fe45eb552cfd` passed v1/v2
  signature and 16 KiB alignment checks, installed, and passed one-child/VPN, TCP/UDP,
  WARP and clean Stop/reconnect gates with stable DE counters.
- Blocker: none.
- Next: none; goal complete.

## Material Decisions

- 2026-08-14: use the existing blocking `Process.waitFor()` as the sole exit event
  source; do not touch the Rust scheduler or carrier protocol.
- 2026-08-14: treat charge-counter results as descriptive unless they exceed short-run
  noise; scheduler mechanism and runtime correctness remain the direct evidence.
- 2026-08-14: after stopping the invalid measurement run, the user explicitly narrowed
  the next action to the `SlipstreamInstance.kt` fix, commit, push, build and APK copy;
  no further baseline or treatment window is required and no battery amount is claimed.

## Checkpoint History

- 2026-08-14: contract frozen after source, goals, Android and DE RECON; R1 started.
- 2026-08-14: invalid baseline evidence was stopped; the focused event-driven change
  compiled, assembled, installed and passed bounded Android lifecycle and payload gates.
- 2026-08-14: runtime commit `31003aa` was pushed and the matching arm64 APK replaced
  the requested destination with an identical digest.

## Completion

- Resolved outcomes: R1 and R3 superseded by the later explicit instruction; R2 and R4
  verified.
- Commands and artifacts: `:app:compileOssReleaseKotlin`,
  `:app:assembleOssRelease`, `apksigner verify`, 16 KiB `zipalign`, Android install,
  bounded Stop/reconnect and TCP/UDP/WARP checks, DE restart-counter comparison,
  commit/push, and matching source/destination APK digests.
- Constraint and diff-scope check: one Kotlin lifecycle owner and this goal changed;
  carrier protocol, scheduler, resolver, server runtime, dependencies and persistent
  telemetry are unchanged. No battery amount is claimed.
- Final status: complete.
