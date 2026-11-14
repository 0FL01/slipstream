# Goal: Reject DNS Tunnel Proxy service mode

Status: complete
Source: user-approved 2026-08-12 W2.2 audit slice
Last updated: 2026-08-12

## Objective

Reproduce and reject DNS Tunnel startup in `Service Mode = Proxy`, preserving its
mandatory application-to-gVisor VPN boundary.

## Execution Directive

Complete the frozen Required Outcomes using the listed Change Envelope and Primary
Evidence. Work on the smallest unresolved outcome. Do not add requirements from
reviews, tests, tools, speculative risks or optional source text. Finish when every
required outcome is resolved and affected constraints remain satisfied.

## Frozen Contract

### Required Outcomes

- R1: DNS Tunnel cannot start outside VPN service mode with gVisor.
  - Source: user-approved W2.2 audit slice.
  - Acceptance: `Service Mode = Proxy` rejects DNS Tunnel before retaining a
    Slipstream child; returning to VPN/gVisor starts one child and carries TCP/UDP.
  - Primary evidence: reproduce a connected Proxy service with no Android VPN before
    the fix, then prove rejected Proxy-mode startup and normal VPN/gVisor E2E.
  - Status: verified
  - Evidence: Android 15 reproduced Proxy service reporting active with one
    Slipstream child, no Android VPN and Firefox direct LTE egress `loc=RU`,
    `warp=off`. The focused production runtime guard rejects Proxy mode with zero
    child/VPN. Returning to VPN/gVisor retained one child, passed TCP and UDP, and
    Firefox reached `loc=DE`, `warp=on`.

### Constraints

- C1: Preserve gVisor-only DNS Tunnel, one child, authenticated carrier and
  fail-closed routing.
- C2: Preserve Proxy service mode for retained non-DNS profiles.
- C3: Do not change server topology, credentials, carrier protocol or olcRTC.
- C4: Do not print or commit tokens, profiles, databases, process arguments, keys or
  traffic logs.

### Non-goals

- Fixing W2.1, W2.3, Wave 3 or other audit findings.
- Removing Proxy service mode globally.
- Redesigning service selection or Android VPN permission UX.

## Change Envelope

- Target: DNS Tunnel runtime compatibility validation before sidecar launch.
- Expected paths and direct consumers:
  - `app/src/main/java/io/nekohasekai/sagernet/bg/proto/V2RayInstance.kt`;
  - `docs/dns-tunnel.md`, the audit document and this goal.
- Allowed artifacts: minimal Kotlin/docs changes and release APK build outputs.
- Forbidden artifacts: dependencies, migrations, services, persistent state, protocol
  or server changes, secret-bearing diagnostics and unrelated fixes.
- User budget: reproduce, fix, focused Android E2E, then commit and push.

## Current Checkpoint

- Closes: R1.
- Smallest next action: commit/push the verified checkpoint and stop.
- Expected evidence: clean pushed tree at the tested source revision.
- Stop or replan if: the final diff changes the tested runtime behavior.

## Current State

- Resolved: production DNS Tunnel startup now requires VPN service mode; foreground
  benchmark ownership remains unchanged.
- Last relevant evidence: fixed arm64 Android E2E with zero child in Proxy mode and
  one child plus TCP/UDP and DE WARP in VPN/gVisor.
- Blocker: none.
- Next: commit/push and stop.

## Material Decisions

- 2026-08-12: enforce the existing gVisor VPN contract in the shared DNS Tunnel
  runtime validator; do not remove Proxy mode from unrelated profile types.

## Checkpoint History

- 2026-08-12: contract frozen; R1 reproduction is current.
- 2026-08-12: R1 reproduced uncaptured direct LTE payload and was closed with a
  production `ProxyInstance` compatibility guard.

## Completion

- Resolved outcomes: R1 verified.
- Commands and artifacts: focused Kotlin compilation, arm64 release assembly, APK
  ZIP/signature/16 KiB alignment checks, Proxy-mode rejection and VPN/gVisor Android
  TCP/UDP plus DE WARP acceptance.
- Constraint and diff-scope check: one-child, authenticated carrier, benchmark and
  non-DNS Proxy-mode behavior remain; no dependency, persistence, protocol, server or
  olcRTC change.
- Final status: complete.
