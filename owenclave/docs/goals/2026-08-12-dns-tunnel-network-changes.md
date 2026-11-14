# Goal: Recover DNS Tunnel across Android network changes

Status: complete
Source: user-approved 2026-08-12 W2.1 and W2.3 audit slices
Last updated: 2026-08-12

## Objective

Close the two remaining DNS Tunnel network-listener defects sequentially: preserve
handover observation across restarts, then react to DNS changes on the same Android
network.

## Execution Directive

Complete the frozen Required Outcomes using the listed Change Envelope and Primary
Evidence. Work on the smallest unresolved outcome. Do not add requirements from
reviews, tests, tools, speculative risks or optional source text. Finish when every
required outcome is resolved and affected constraints remain satisfied. Reproduce,
fix, validate, commit and push R1 before starting R2.

## Frozen Contract

### Required Outcomes

- R1: Restart cleanup cannot unregister the replacement VPN network listener.
  - Source: user-approved W2.1.
  - Acceptance: at least two consecutive Wi-Fi/LTE handovers each restart DNS Tunnel,
    retain one child and recover controlled payload.
  - Primary evidence: reproduce the stale-stop ordering or missed second handover,
    then prove child replacement and payload after two fixed-build handovers.
  - Status: verified
  - Evidence: Android 15 reproduced Wi-Fi to LTE replacing the child, then LTE to
    Wi-Fi stopping the VPN instead of recovering. Unique listener-session keys prevent
    stale teardown from removing a replacement, and teardown resets the old underlay
    baseline. With strict `tcp://77.88.8.8:53` to isolate listener ownership, both
    fixed handovers replaced the child, retained one VPN/child and reached DE WARP.

- R2: Automatic DNS Tunnel reacts when DNS changes on the same Android Network.
  - Source: user-approved W2.3.
  - Acceptance: a same-network link-properties DNS change restarts automatic DNS
    Tunnel and takes a fresh resolver snapshot; manual mode remains pinned.
  - Primary evidence: controlled same-Network callback reproduction followed by
    automatic child replacement/payload and no manual-mode restart.
  - Status: superseded
  - Evidence: the user closed W2.3 after bounded Android 15 attempts could not produce
    a same-Network `LinkProperties.dnsServers` change. Private DNS did not change the
    resolver list or child, and no router/server settings were altered. The unverified
    callback implementation was removed rather than shipped without runtime evidence.

### Constraints

- C1: Preserve gVisor-only VPN mode, one child/resolver, authenticated carrier and
  fail-closed routing.
- C2: Preserve strict manual resolver behavior and automatic resolver ordering.
- C3: Do not change live router/server DNS, topology, credentials, carrier protocol or
  olcRTC.
- C4: Do not print or commit tokens, profiles, databases, process arguments, keys or
  traffic logs.
- C5: Commit and push R1 before editing R2.

### Non-goals

- Wave 3 and additional audit findings.
- General network monitoring redesign or periodic health checks.
- Disrupting the live public DNS carrier.

## Change Envelope

- Target: VPN ownership of `DefaultNetworkListener` and same-network DNS callbacks.
- Expected paths and direct consumers:
  - `app/src/main/java/io/nekohasekai/sagernet/bg/VpnService.kt`;
  - `app/src/main/java/io/nekohasekai/sagernet/utils/DefaultNetworkListener.kt`;
  - focused instrumentation only if direct runtime evidence cannot distinguish the
    required ordering;
  - `docs/dns-tunnel.md`, the audit document and this goal.
- Allowed artifacts: minimal Kotlin/docs changes and release APK build outputs.
- Forbidden artifacts: dependencies, migrations, services, persistent state, protocol
  or server changes, secret-bearing diagnostics and unrelated fixes.
- User budget: one outcome per checkpoint, focused E2E, separate commit and push.

## Current Checkpoint

- Closes: R2 by explicit user supersession.
- Smallest next action: verify cleanup and stop this objective.
- Expected evidence: no W2.3 runtime diff remains and R1 stays pushed.
- Stop or replan if: cleanup removes the verified R1 listener ownership fix.

## Current State

- Resolved: R1 verified; R2 superseded by the user after its controlled trigger could
  not be produced without changing network infrastructure.
- Last relevant evidence: R1 remains at `fab79d9`; W2.3 source changes were removed.
- Blocker: none.
- Next: begin the separately approved Wave 3 objective.

## Material Decisions

- 2026-08-12: execute W2.1 before W2.3 with a separate verified commit/push.
- 2026-08-12: do not alter live infrastructure to force same-network DNS changes.

## Checkpoint History

- 2026-08-12: contract frozen; R1 is current.
- 2026-08-12: R1 reproduced and closed with per-session listener ownership plus fresh
  restart baseline; automatic timing was deliberately excluded from this checkpoint.
- 2026-08-12: bounded W2.3 attempts did not alter same-Network DNS. The user closed the
  slice; speculative callback changes were discarded and no infrastructure was changed.

## Completion

- Resolved outcomes: R1 verified; R2 superseded by explicit user direction.
- Commands and artifacts: Android 15 two-handover acceptance for R1 and source cleanup
  against `fab79d9` for R2.
- Constraint and diff-scope check: verified R1 retained; no W2.3 runtime change, network
  infrastructure change, secret or new dependency remains.
- Final status: complete.
