# Goal: Close DNS Tunnel Wave 3 lifecycle defects

Status: complete
Source: user-approved 2026-08-12 Wave 3 audit slice
Last updated: 2026-08-12

## Objective

Reproduce each Wave 3 finding under controlled fault injection, fix only confirmed
defects, and leave DNS Tunnel fail-closed with bounded process and connection ownership.

## Execution Directive

Complete the frozen Required Outcomes using the listed Change Envelope and Primary
Evidence. Work on the smallest unresolved outcome. Do not add requirements from
reviews, tests, tools, speculative risks or optional source text. Finish when every
required outcome is resolved and affected constraints remain satisfied. Reproduce,
fix, build, validate, commit and push each confirmed defect before starting the next.

## Frozen Contract

### Required Outcomes

- R1: A replacement Slipstream child must become ready or stop the stale Connected
  service state.
  - Source: user-approved W3.1.
  - Acceptance: after the ready child exits against a controlled unavailable resolver,
    the service cannot remain Connected indefinitely with a live unready replacement.
  - Primary evidence: disposable TCP resolver reproduction, then bounded service state,
    child count and payload observation on the fixed build.
  - Status: verified
  - Evidence: Android 15 connected through a disposable localhost TCP DNS forwarder.
    On the baseline, killing the ready child and removing the forwarder produced a new
    live child while the service stayed Connected beyond the normal readiness bound.
    The fixed build invokes a full service restart: the replacement uses the existing
    15-second startup readiness gate and VPN/child reached zero when it stayed unready.

- R2: Failed captured gVisor TCP dials release their connection state immediately.
  - Source: user-approved W3.2.
  - Acceptance: bounded failed dials do not retain the incoming connection, context or
    connection-list entry until VPN shutdown.
  - Primary evidence: controlled failing dial reproduction with before/after resource
    observation or a focused deterministic core test.
  - Status: verified
  - Evidence: the focused `TestNewConnectionClosesFailedDial` reproduced one retained
    list entry and open incoming connection on the baseline. The fixed common return
    cleanup cancels context, removes the entry and closes the incoming connection;
    focused and full `CGO_ENABLED=0 go test ./...` passed. The rebuilt arm64 core was
    packaged into a signed/aligned release, and Android 15 kept one VPN/child with
    Firefox payload at `loc=DE`, `warp=on`.

- R3: A final stop request wins over an already queued restart.
  - Source: user-approved W3.3.
  - Acceptance: user/fatal stop received during restart teardown leaves the service
    Stopped and does not start another VPN session.
  - Primary evidence: controlled teardown barrier reproducing the crossed requests,
    then the same ordering on the fixed build.
  - Status: verified
  - Evidence: a temporary five-second teardown barrier reproduced the crossed order:
    `RELOAD` then `CLOSE` still replaced child PID 14363 with 14854 and retained the
    VPN. The fixed build records a final stop during `Stopping`; the same sequence
    removed child PID 15231 and VPN state, and neither returned during 12 seconds.
    The barrier and debuggable build flag were removed before the final build.

### Constraints

- C1: Preserve gVisor-only VPN mode, one child/resolver, authenticated carrier and
  fail-closed routing.
- C2: Do not disrupt the public DNS carrier or change router/server configuration,
  credentials, carrier protocol or olcRTC.
- C3: Do not print or commit tokens, complete profiles, databases, process arguments,
  keys or traffic logs.
- C4: Fault-injection hooks are temporary evidence only and must not remain in runtime
  artifacts or commits.
- C5: Commit and push each confirmed defect before editing the next one.

### Non-goals

- Reopening W2.3 or fixing additional audit findings.
- General lifecycle redesign, health polling or retry framework.
- Supporting a direct fallback after carrier failure.

## Change Envelope

- Target: post-readiness Slipstream ownership, gVisor TCP failure cleanup and service
  restart/stop intent ordering.
- Expected paths and direct consumers:
  - `app/src/main/java/io/nekohasekai/sagernet/bg/proto/SlipstreamInstance.kt`;
  - `app/src/main/java/io/nekohasekai/sagernet/bg/BaseService.kt`;
  - `library/core/tun.go` and focused tests if practical;
  - temporary controlled fault-injection code, removed before commit;
  - `docs/dns-tunnel.md`, the audit document and this goal.
- Allowed artifacts: minimal Kotlin/Go/docs changes and release APK build outputs.
- Forbidden artifacts: dependencies, migrations, services, persistent state, protocol
  or server changes, secret-bearing diagnostics and unrelated fixes.
- User budget: one outcome per checkpoint, focused E2E, separate commit and push.

## Current Checkpoint

- Closes: R1-R3.
- Smallest next action: commit/push the final verified checkpoint and stop.
- Expected evidence: clean branch at the pushed R3 commit.
- Stop or replan if: the tracked closure diff changes.

## Current State

- Resolved: R1-R3. Carrier recovery is readiness-gated, failed TCP dials release their
  owner, and final stop intent overrides queued restart intent.
- Last relevant evidence: final non-debug arm64 release retained one child/VPN and
  Firefox payload at `loc=DE`, `warp=on`; stop left zero child/VPN.
- Blocker: none.
- Next: commit/push and stop.

## Material Decisions

- 2026-08-12: execute W3.1, W3.2 and W3.3 sequentially; only reproduced defects receive
  runtime fixes.
- 2026-08-12: use disposable local forwarding and temporary barriers rather than alter
  live router/server behavior.

## Checkpoint History

- 2026-08-12: contract frozen; R1 is current.
- 2026-08-12: R1 reproduced and fixed by routing post-readiness exit through the
  existing full-service readiness and failure path.
- 2026-08-12: R2 reproduced with a deterministic failed core dial and fixed by common
  deferred cancellation, list removal and incoming connection close.
- 2026-08-12: R3 reproduced with a temporary teardown barrier and fixed by preserving
  mutable restart intent while cleanup is in progress.

## Completion

- Resolved outcomes: R1-R3 verified.
- Commands and artifacts: focused/full core Go tests and module verification, clean
  arm64 core build, `:app:assembleOssRelease`, APK ZIP/signature/16 KiB alignment,
  controlled Android fault injection and final DNS Tunnel payload acceptance.
- Constraint and diff-scope check: gVisor-only, one child, authenticated carrier and
  fail-closed routing remain; no server, protocol, credential, dependency or olcRTC
  changes, and all temporary fault hooks were removed.
- Final status: complete.
