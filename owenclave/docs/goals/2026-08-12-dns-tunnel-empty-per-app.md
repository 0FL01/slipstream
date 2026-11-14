# Goal: Fix empty DNS Tunnel per-app Proxy mode

Status: complete
Source: user-approved 2026-08-12 W1.5 audit slice
Last updated: 2026-08-12

## Objective

Reproduce and fix the empty per-app `Proxy` list so DNS Tunnel cannot silently
capture every application or recurse its own carrier traffic.

## Execution Directive

Complete the frozen Required Outcomes using the listed Change Envelope and Primary
Evidence. Work on the smallest unresolved outcome. Do not add requirements from
reviews, tests, tools, speculative risks or optional source text. Finish when every
required outcome is resolved and affected constraints remain satisfied.

## Frozen Contract

### Required Outcomes

- R1: Empty per-app `Proxy` mode cannot start as an all-application VPN.
  - Source: user-approved W1.5 audit slice.
  - Acceptance: starting DNS Tunnel with per-app `Proxy` enabled and no selected
    packages fails before a VPN or Slipstream child is retained; a non-empty list
    still captures only the selected application and carries its payload normally.
  - Primary evidence: reproduce the empty-list scope inversion before the fix, then
    verify fail-closed empty-list startup plus one-selected-app Android E2E.
  - Status: verified
  - Evidence: Android 15 reproduced an empty `Proxy` list creating a VPN and one
    Slipstream child before both stopped after the carrier readiness window. The fix
    rejects the empty DNS Tunnel allowlist before retaining a VPN or child. With only
    Firefox selected, one child/VPN remained active, Firefox reached `loc=DE`,
    `warp=on`, and unselected Termux received no external response on restricted LTE.

### Constraints

- C1: Preserve gVisor-only DNS Tunnel, one child, authenticated carrier, per-app
  `Proxy` allowlist and `Bypass` denylist semantics.
- C2: Do not change server topology, credentials, carrier protocol, olcRTC or Android
  package selections outside the bounded acceptance setup.
- C3: Do not print or commit tokens, profiles, databases, process arguments, keys or
  traffic logs.

### Non-goals

- Fixing Wave 2/3 or other audit findings.
- Redesigning Android per-app routing or service error handling.
- Disruptive failure injection against the live carrier.

## Change Envelope

- Target: Android VPN per-app validation before `VpnService.Builder.establish()`.
- Expected paths and direct consumers:
  - `app/src/main/java/io/nekohasekai/sagernet/bg/VpnService.kt`;
  - `docs/android-network-routing.md`, the audit document and this goal.
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

- Resolved: empty DNS Tunnel `Proxy` mode now fails before VPN creation; a one-app
  allowlist retains normal selected-app carrier behavior.
- Last relevant evidence: fixed arm64 release Android E2E with zero resources for the
  empty list and one child plus DE WARP for selected Firefox.
- Blocker: none.
- Next: commit/push and stop.

## Material Decisions

- 2026-08-12: reject empty `Proxy` startup rather than reinterpret it as per-app Off;
  Off captures all by design and would preserve the unsafe scope inversion.

## Checkpoint History

- 2026-08-12: contract frozen; R1 reproduction is current.
- 2026-08-12: R1 reproduced the capture-all carrier recursion and was closed with
  validation immediately before Android applies the per-app allowlist.

## Completion

- Resolved outcomes: R1 verified.
- Commands and artifacts: focused Kotlin compilation, arm64 release assembly, APK
  ZIP/signature/16 KiB alignment checks, empty-list rejection and one-app Android E2E.
- Constraint and diff-scope check: gVisor-only, one-child, authenticated carrier,
  `Bypass` and non-empty `Proxy` semantics remain; no dependency, persistence,
  protocol, server or olcRTC change.
- Final status: complete.
