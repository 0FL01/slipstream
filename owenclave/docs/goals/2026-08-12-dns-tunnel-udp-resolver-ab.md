# Goal: Test the same DNS Tunnel resolver over UDP

Status: complete
Source: user-approved throughput RECON follow-up, 2026-08-12
Last updated: 2026-08-12

## Objective

Record the three RECON performance vectors, then finish only the first vector: compare
the current strict TCP resolver with the same host and port over UDP on the actual
gVisor VPN path, retaining UDP only if it passes the frozen throughput, latency,
recovery and egress gates.

## Execution Directive

Complete the frozen Required Outcomes using the listed Change Envelope and Primary
Evidence. Work on the smallest unresolved outcome. Do not add requirements from
reviews, tests, tools, speculative risks or optional source text. Finish when every
required outcome is resolved and affected constraints remain satisfied.

## Frozen Contract

### Required Outcomes

- R1: Preserve the three evidence-ranked RECON vectors in the repository.
  - Source: user instruction to document all three plan variants.
  - Acceptance: this document records each mechanism, evidence, narrow experiment,
    pass gate and rollback without promoting vectors 2 or 3 into the current runtime
    scope.
  - Primary evidence: the `Performance Vectors` section below.
  - Status: verified
  - Evidence: vectors 1-3 are recorded below; only vector 1 entered this goal's runtime
    scope and is now closed as a rejected dead end.

- R2: Complete a short TCP-versus-UDP A/B for one resolver identity.
  - Source: user instruction to test `Тот же resolver через UDP вместо TCP` from start
    to finish.
  - Acceptance: the current strict `tcp://host:port` control and strict
    `udp://same-host:same-port` treatment each run through one child and the real gVisor
    VPN; exact download, acknowledged upload and 4 KiB latency results are recorded.
  - Primary evidence: paired exact-byte Android payload timings on the same LTE radio,
    position and endpoint, with one child/VPN observed for each transport.
  - Status: verified
  - Evidence: strict TCP and UDP used the same resolver host and port on the same LTE
    radio through one gVisor VPN and child. TCP exact 1 MiB downloads completed at
    200.017, 199.095 and 192.097 kB/s (mean 197.070 kB/s); UDP completed at 18.260,
    18.918 and 17.184 kB/s (mean 18.121 kB/s), 90.8% below TCP. TCP's first 512 KiB
    upload bound timed out at 25 seconds, then a longer bounded confirmation returned
    HTTP 200 in 31.137 seconds at 16.838 kB/s. UDP sent the same 512 KiB but returned
    no final response within 70 seconds, so 7.489 kB/s is only a failed wall-clock
    lower bound. TCP and UDP exact 4 KiB probes took 493 ms and 2.300 seconds. UDP
    cancellation recovery remained functional at 1.793 seconds with one child/VPN.

- R3: Retain only the transport justified by the frozen gates.
  - Source: user instruction to finish vector 1 and report the result.
  - Acceptance: retain UDP only when paired download improves at least 15%, a final
    successful upload response is no worse than TCP by more than 5%, 4 KiB latency is
    no worse than 10%, cancellation/recovery succeeds and DE/WARP egress is unchanged;
    otherwise restore the original TCP resolver. Stop and record failure if UDP cannot
    become ready.
  - Primary evidence: final selected profile transport, one-child payload/recovery,
    Stop to zero child/VPN and stable DE service restart counters.
  - Status: verified
  - Evidence: UDP failed throughput, upload-response and latency gates and is a dead
    end for this same-resolver performance vector. The profile was restored to strict
    TCP on the original host/port. Restored TCP retained one child/VPN, returned the
    expected DE WARP egress and an exact 4 KiB HTTP 200 in 558 ms, then Stop left zero
    child/VPN. DE `slipstream.service` and `flowd.service` remained active and restart
    counters stayed 5 and 0.

### Constraints

- C1: Change only the resolver transport. Keep the exact resolver host and port, one
  resolver, one child, one carrier path, gVisor capture, authenticated FlowRelay and
  fail-closed WARP egress.
- C2: Do not print or commit the Flow token, complete profile, credentials, resolver
  database or payload-bearing logs.
- C3: Keep Busy/Warm/Quiescent/Empty scheduling, Rust artifact, adapter constants,
  server, FlowRelay, WARP, listener and firewall unchanged.
- C4: LTE is currently unrestricted for direct external traffic. This is a same-radio
  forced-tunnel A/B, not Restricted LTE traversal acceptance.
- C5: Keep each transport window under nine minutes; do not run a 30-60 minute test.

### Non-goals

- Implementing vectors 2 or 3 in this goal.
- Another queue-size treatment, more workers/retries/timeouts, multipath, resolver
  fallback, pipelining, congestion-control work or server changes.
- Treating the foreground resolver benchmark as production gVisor evidence.

## Performance Vectors

### 1. Same resolver over UDP instead of TCP — rejected dead end

- Mechanism: the existing manual `udp://` path bypasses `DnsTcpAdapter`, its bounded
  queue, 32 sequential TCP exchanges and observed `trySend` loss while preserving one
  resolver, child and server path.
- Evidence: the control adapter dropped 412 of 3,409 accepted datagrams (12.09%); a
  64-to-128 queue increase left 21.46% drops and only +2.7% single-run download.
- Experiment: pair the current TCP endpoint with UDP on the same host/port, first using
  readiness and one exact download as a fail-fast gate, then the frozen payload set.
- Pass/rollback: use the R3 gates; restore the original strict TCP selection on any
  failure. UDP reachability on unrestricted LTE does not prove Restricted LTE support.
- Result: UDP was reachable and functional but averaged only 18.121 kB/s versus TCP
  197.070 kB/s, failed the acknowledged-upload gate and made the 4 KiB probe 4.66x
  slower. Do not repeat this transport-only performance experiment for the same
  resolver/path without materially new network evidence.

### 2. Bound only the Rust Busy poll burst — deferred

- Mechanism: preserve normal packet generation and quiet scheduling, but cap the
  additional Busy `send_poll_queries` burst so a scheduler turn cannot flood the
  64-slot Android adapter queue after the ordinary authoritative send loop.
- Evidence: pinned authoritative mode can emit up to 40 ordinary packets and then up
  to 40 poll-path packets per turn; low lifetime worker occupancy does not exclude such
  microbursts. Measured Busy underfill was only 10.6%, so more poll supply is wrong.
- Experiment: one aggregate-only A/B with an additional Busy poll cap of 8, unchanged
  Warm/Quiescent/Empty behavior and synchronized workload counters.
- Pass/rollback: paired throughput +15%, adapter drops at most 0.1%, expiry/loss per
  useful MiB down at least 50% and 4 KiB latency no worse than 10%; otherwise remove
  the patch and restore the pinned artifact.

### 3. Budget polls using all unresolved DNS transactions — deferred

- Mechanism: track data-bearing and pure-poll DNS IDs until response or expiry, then
  create Busy polls from `target - all_dns_outstanding` under existing cwnd, pacing and
  burst caps.
- Evidence: measured Busy averages were about 388 unresolved DNS transactions versus
  target 107; current `inflight_poll_ids` covers only the explicit poll path, while
  normal data-bearing queries are absent from that external transaction budget.
- Experiment: only after vector 2, use class-separated synchronized counters and one
  local outstanding-aware budget diff; do not retransmit identical DNS queries.
- Pass/rollback: drops at most 0.1%, expiry below 2%, queries/useful MiB lower, download
  +15%, upload no worse than 5% and quiet scheduling unchanged; otherwise restore the
  current budget.

## Short A/B Protocol

1. Keep Wi-Fi off, one LTE radio/position and one fixed external endpoint. Use the
   current installed non-instrumented release.
2. For each transport, start a fresh child, prove one VPN/child and run one unscored
   128 KiB warm-up.
3. Score three exact 1 MiB downloads. Score one exact 512 KiB upload with fixed content
   length and a required final 2xx; a timeout is a failure, not a speed sample.
4. Run one exact 4 KiB download, cancel a short bulk transfer, then require another
   exact 4 KiB response and one retained child/VPN.
5. Record every result without retry replacement. Stop early on readiness failure,
   wrong bytes/status, child restart, radio change or wrong egress.

## Current Checkpoint

- Closes: R1-R3.
- Smallest next action: commit the completed evidence-only checkpoint and pause.
- Expected evidence: clean branch at the documentation commit with strict TCP retained.
- Stop or replan if: tracked runtime or selected profile transport changes.

## Current State

- Resolved: R1-R3. Same-resolver UDP is a measured performance dead end; TCP is
  restored and vectors 2-3 remain deferred.
- Last relevant evidence: UDP download was 90.8% below TCP, upload did not complete and
  4 KiB latency was 4.66x worse; restored TCP payload, egress and Stop passed.
- Blocker: none.
- Next: commit and pause before any separately approved vector 2 work.

## Material Decisions

- 2026-08-12: vector 1 needs no code or native build because manual UDP is already a
  stable supported transport; production gVisor payload is the evidence surface.
- 2026-08-12: use exact 1 MiB downloads and one 512 KiB acknowledged upload to keep the
  complete A/B short while retaining a directional and latency check.
- 2026-08-12: reject UDP as a same-resolver performance path. Reachability alone was
  insufficient: all three throughput/latency gates materially favored TCP.

## Checkpoint History

- 2026-08-12: goal activated from the user-approved RECON; vectors 1-3 frozen and only
  the same-resolver UDP A/B admitted into the current scope.
- 2026-08-12: R2-R3 closed. UDP remained functional but averaged 18.121 kB/s against
  TCP's 197.070 kB/s, failed the upload-response gate and regressed 4 KiB latency;
  original strict TCP was restored and accepted.

## Completion

- Resolved outcomes: R1 documents all three vectors; R2 completes same-resolver
  TCP/UDP evidence; R3 rejects UDP, restores TCP and verifies payload/recovery/Stop.
- Commands and artifacts: Android UI transport selection, exact Termux curl payloads,
  one-child/VPN observations, Cloudflare DE/WARP trace and DE service/restart checks.
- Constraint and diff-scope check: no runtime, native, server, resolver identity,
  credential or protocol change is retained; only documentation changed.
- Final status: complete; vector 1 is a rejected dead end and vectors 2-3 are paused.
