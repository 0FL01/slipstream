# Goal: Close confirmed DNS Tunnel defects

Status: complete
Source: user-approved 2026-08-12 Wave 1 defect list and iterative delivery order
Last updated: 2026-08-12

## Objective

Reproduce and fix four DNS Tunnel routing and ownership defects one at a time, proving
each fix end to end and committing it before starting the next defect.

## Execution Directive

Complete the frozen Required Outcomes using the listed Change Envelope and Primary
Evidence. Work on the smallest unresolved outcome. Do not add requirements from
reviews, tests, tools, speculative risks or optional source text. Finish when every
required outcome is resolved and affected constraints remain satisfied. Reproduce,
fix, validate, commit and push each outcome separately before continuing.

## Frozen Contract

### Required Outcomes

- R1: DNS Tunnel cannot be bypassed by global Route Mode `Direct`.
  - Source: user defect 1.
  - Acceptance: DNS Tunnel application and benchmark payload always use its carrier;
    `Direct` cannot select the bypass outbound for that profile.
  - Primary evidence: reproduce direct egress before the fix, then prove the same
    controlled payload has DE WARP egress through one Slipstream child after the fix.
  - Status: verified
  - Evidence: Android 15 with Route Mode `Direct` reproduced direct LTE egress
    `loc=RU`, `warp=off` while VPN and one Slipstream child were active. The fixed
    build kept one child and returned `loc=DE`, `warp=on`; TCP and UDP probes passed,
    and n-de1 restart counters remained 5/0.

- R2: `Test all` cannot overlap an active DNS Tunnel.
  - Source: user defect 2.
  - Acceptance: while DNS Tunnel is starting or connected, `Test all` cannot launch a
    second DNS test child or alter the production certificate; production payload and
    controlled restart remain healthy.
  - Primary evidence: reproduce overlap before the fix, then observe at most one child
    plus successful payload and restart after the fix.
  - Status: verified
  - Evidence: Android 15 reproduced two concurrent Slipstream children immediately
    after Test all was pressed beside a connected DNS Tunnel VPN. The focused UI fix
    disables the action while the service is starting or connected and rechecks that
    state in the handler. The fixed action launched no batch test and retained exactly
    one child; DE WARP, TCP/UDP payload and a controlled VPN restart passed.

- R3: closing an active DNS benchmark waits for cleanup before Connect is reachable.
  - Source: user defect 3.
  - Acceptance: Cancel keeps the benchmark owner visible until its child is stopped;
    immediate Connect cannot overlap benchmark teardown.
  - Primary evidence: reproduce the cleanup window before the fix, then prove ordered
    zero-child transition followed by one-child VPN payload after the fix.
  - Status: verified
  - Evidence: before the fix, Cancel removed the benchmark dialog as its independent
    cancellation cleanup began, so Connect became immediately reachable without a UI
    cleanup barrier. The focused fix keeps the dialog running until `cancelAndJoin`
    completes. Android observed the dialog hidden only with zero child; immediate
    Connect then reached one child/VPN and TCP/UDP payload passed.

- R4: rapid benchmark resolver choices persist the final user action.
  - Source: user defect 4.
  - Acceptance: repeated resolver/Automatic choices have one accepted owner, so the
    last accepted tap is the persisted mode; no benchmark child remains.
  - Primary evidence: reproduce out-of-order persistence before the fix, then repeat a
    bounded alternating-choice sequence and verify the final displayed selection.
  - Status: verified
  - Evidence: Android reproduced a completed resolver tap followed immediately by
    Automatic, but the earlier resolver was persisted even though both controls
    accepted taps. The focused fix accepts exactly one choice and disables all
    selectors until cleanup/persistence finish. Rapid resolver/Automatic and
    Automatic/resolver sequences each accepted one owner, persisted that first/last
    accepted tap and left zero child.

### Constraints

- C1: Preserve gVisor-only DNS Tunnel, one resolver/child, authenticated loopback
  SOCKS/UoT, fail-closed routing and existing benchmark persistence behavior.
- C2: Do not change server topology, credentials, carrier wire formats or olcRTC.
- C3: Do not print or commit tokens, complete profiles, databases, process arguments,
  keys or traffic logs.
- C4: Commit and push only after each defect passes its focused E2E acceptance.

### Non-goals

- Fixing audit findings outside the four defects listed by the user.
- Refactoring shared routing, test or persistence architecture beyond the minimum
  required by a reproduced defect.
- Disruptive fail-closed testing against the live public carrier.

## Change Envelope

- Target: Owenclave DNS Tunnel config generation and Compose test/benchmark ownership.
- Expected paths and direct consumers:
  - `app/src/main/java/io/nekohasekai/sagernet/fmt/ConfigBuilder.kt`;
  - `app/src/main/java/io/nekohasekai/sagernet/ui/compose/screens/ConfigurationScreen.kt`;
  - directly affected focused UI/config tests if an existing location is suitable;
  - `docs/dns-tunnel.md`, the audit document and this goal when stable behavior changes.
- Allowed artifacts: minimal Kotlin/docs changes and release APK build outputs.
- Forbidden artifacts: new dependencies, migrations, services, persistent stores,
  protocol changes, server changes, secret-bearing diagnostics or unrelated fixes.
- User budget: one defect per checkpoint; focused E2E, then a separate commit and push.

## Current Checkpoint

- Closes: R1-R4.
- Smallest next action: commit/push this final checkpoint, replace the requested APK
  and stop.
- Expected evidence: clean pushed tree and matching source/destination APK hashes.
- Stop or replan if: final delivery changes the verified artifact.

## Current State

- Resolved: R1-R4. Direct cannot bypass DNS Tunnel, Test all cannot overlap an active
  service, Cancel joins cleanup, and benchmark selection has one accepted owner.
- Last relevant evidence: both rapid selection orders persisted the only accepted
  control and left zero child; release assembly passed.
- Blocker: none.
- Next: commit/push R4, replace the requested APK and stop.

## Material Decisions

- 2026-08-12: execute in the user-specified order and do not batch fixes.
- 2026-08-12: each bug receives its own successful E2E checkpoint and commit/push.

## Checkpoint History

- 2026-08-12: contract frozen; R1 is current and no runtime fix has been made.
- 2026-08-12: R1 reproduced direct LTE bypass and closed it with a one-condition
  config fix; Direct-mode DNS Tunnel E2E passed through DE WARP.
- 2026-08-12: R2 reproduced two concurrent Slipstream children; Test all is now
  disabled and guarded while the proxy service is starting or connected.
- 2026-08-12: R3 confirmed that Cancel had no UI cleanup barrier; the dialog now stays
  visible until benchmark cancellation and child teardown join.
- 2026-08-12: R4 reproduced ambiguous double acceptance; the first selection now owns
  cleanup/persistence and all other selectors disable immediately.

## Completion

- Resolved outcomes: R1-R4 verified.
- Commands and artifacts: four focused Android reproductions/fixes; release assembly,
  ZIP integrity, v1/v2 signature and 16 KiB alignment; LTE DNS Tunnel TCP/UDP payload,
  DE WARP egress and n-de1 stability checks.
- Constraint and diff-scope check: gVisor-only, one-child, authenticated carrier and
  fail-closed contracts remain; no server, credential, wire-format, olcRTC, dependency
  or persistent-schema change.
- Final status: complete.
