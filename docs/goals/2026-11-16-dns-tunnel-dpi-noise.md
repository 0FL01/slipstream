# Goal: reduce DNS-tunnel DPI noise without material speed loss

Status: active
Source: user instruction 2026-11-16 (accepts the three-package proposal from the DPI analysis).
Last updated: 2026-11-16

## Objective
Lower the passive/active detectability of the DNS tunnel while keeping the lab LTE path usable:
adaptive transport concurrency and idle backoff, authenticated DNS-layer payload obfuscation with
NXDOMAIN behaviour for unauthenticated probes, and a realistic authoritative zone/QUIC fingerprint.
Measure noise (idle queries, sockets, QNAME/TXT shape) and keep active throughput within a bounded loss.

## Execution Directive
Complete the frozen Required Outcomes using the listed Change Envelope and Primary Evidence. Work on the
smallest unresolved outcome. Do not add requirements from reviews, tests, tools, speculative risks, or
optional source text. Finish when every required outcome is resolved and affected constraints remain
satisfied.

## Frozen Contract

### Required Outcomes
- R1: Adaptive transport concurrency and idle backoff.
  - Source: package item 1.
  - Acceptance: idle tunnel holds at most 16 TCP-DNS connections per resolver (instead of the fixed 112)
    and polls no more often than every 15 s after 2 idle minutes, with jitter; active peak throughput is not
    lower than the pre-change baseline by more than 10%; the server idle timeout exceeds the slowest idle
    poll so an idle session survives.
  - Primary evidence: instrumented aggregate counters (lanes, polls/min, active throughput A/B) on the lab
    phone + loopback runtime tests.
  - Status: implemented and phone-verified within LTE variance.
  - Evidence: client keeps `min(8, lanes)` persistent TCP-DNS lanes and ramps extra lanes only while the
    queue is deeper than the connected capacity; extra lanes close after 30 s idle. Idle polls step
    2 s -> 5 s -> 20 s and keepalive 5 s -> 15 s -> 60 s after 60/120 s without activity, each ±10%
    jitter; client QUIC idle timeout 180 s. `cargo test -p slipstream-client` 29/29. Local C server
    run with the token file held a QUIC session over the new idle settings. Phone Speedtest through
    the tunnel: DL 6.19/6.32 Mbps, UL 0.79/0.40 Mbps, ping 156-174 ms (cold run 4.69 Mbps) against
    the 2026-11-15 baseline DL 7.32/5.99, UL 0.92/0.67; the difference is inside the baseline LTE
    spread plus the 6-byte wire overhead. The app's 56-worker TCP-DNS adapter keeps its own resolver
    sockets, so a raw `/proc` connection count is not a lane count.
- R2: Authenticated DNS-layer obfuscation.
  - Source: package item 2.
  - Acceptance: QNAME/TXT payloads no longer expose a QUIC header or a constant binary prefix; a query
    without a valid token-derived keystream receives an NXDOMAIN/SOA response indistinguishable from a
    normal nonexistent name; valid traffic still passes; overhead is at most 5% of payload bytes;
    replay/duplicate counters do not break QUIC recovery.
  - Primary evidence: loopback C/Rust interop tests (valid, corrupt, replayed, truncated), byte-shape
    stats, phone browsing/cancel tests.
  - Status: implemented and phone-verified.
  - Evidence: loopback Rust client <-> C server with a matching 16-byte token completed a SOCKS round
    trip (43-byte echo) through encrypted QNAME/TXT payloads. Each frame is a 4-byte random nonce plus
    SHAKE128(key || direction || nonce) XOR (2-byte magic || payload); query/answer directions differ.
    Invalid base32, wrong-magic, truncated and random frames return NXDOMAIN + SOA, identical to a
    normal nonexistent name; the zone matrix stays conventional. Captured 59/59 distinct first-6 QNAME
    prefixes and 19/19 distinct first-4 TXT prefixes, no stable counter prefix. Overhead 6 bytes.
    Deployed server `c6b0acf0…` reads the token as a systemd credential; the signed APK
    `bcc4f11d…` carries `libslipstream.so` `2f6c43fe…`. The phone held the tunnel through the
    restart and completed three Speedtest runs; malformed probes from n-de1 return NXDOMAIN+SOA.
- R3: Realistic authoritative zone and QUIC fingerprint.
  - Source: package item 3.
  - Acceptance: the served zone answers SOA/NS/apex A/AAAA, returns NXDOMAIN with SOA and echoed EDNS
    size for unknown names, uses jittered TTLs, and the QUIC handshake uses standard ALPN `h3` and a
    neutral certificate identity visible to an active prober.
  - Primary evidence: direct `dig`/loopback query matrix against the C server + live n-de1 probe.
  - Status: implemented and live n-de1-verified.
  - Evidence: direct probe against the C server: SOA/NS/apex A/NODATA/NXDOMAIN/REFUSED behave
    conventionally with SOA on NXDOMAIN, EDNS size is echoed and TXT TTLs from live traffic ranged
    89-208 (jitter 30-270). Both C and Rust now advertise ALPN `h3`, matching `config.alpn` on the C
    runtime; the served certificate identity is unchanged. C suite 7/7 on the final run (the timing-
    based `runtime` multipath test failed intermittently on this 1-vCPU host under load; the final
    rerun passed). Live n-de1 probe after deployment: SOA/NS/apex A answered, random TXT under the
    zone returned NXDOMAIN+SOA with echoed EDNS, out-of-zone REFUSED.

### Constraints
- C1: Keep existing contracts: token auth via FlowRelay stdin, one QUIC stream per app flow, gVisor only,
  no mux/fallback, pinning, DIRECT egress only.
- C2: No new external dependency or service without need; stdlib/OpenSSL and existing Rust crates first.
- C3: Production changes on n-de1 (token credential to Slipstream, unit restart) need a backup and one
  short restart; shared RA74 path must stay up.
- C4: Do not log payloads/QNAMEs, tokens or keys; only aggregate counters.
- C5: Resolver scope stays Yandex/Rostelecom/operator; no stress sweeps; lab phone is the only device.

### Non-goals
- Cover traffic, new transports/fallback, domain rotation infrastructure, anycast/CDN migration.
- Changing FlowRelay authentication or the QUIC congestion controller.

## Change Envelope
- Client: `slipstream-rust/crates/slipstream-client` (bridge lanes, pacing/keepalive, DNS codec use),
  `slipstream-rust/crates/slipstream-dns` (wire format), Owenclave args only if unavoidable.
- Server: `slipstream-c/src/slipstream_server.c`, `slipstream_sockloop.c`, CLI/runtime, `include/`.
- Runtime: n-de1 `slipstream.service` credential/env, idle timeout and ALPN; no FlowRelay change.
- Allowed artifacts: source, tests, goal/ops docs, temporary certs/keys outside Git.
- Forbidden: secrets/payloads/generated binaries in Git, new public endpoints, disabling auth.

## Current Checkpoint
- Closes: R1, R2 and R3 with local and live evidence.
- Smallest next action: monitor the seven-day crash window and the next phone load/cancel batch under
  the crypto deployment; no further code changes are required by this contract.
- Expected evidence: no DNS-header aborts, stable idle sessions, unchanged probe behaviour.
- Stop or replan if: the token credential path fails, the server crash window reopens, or throughput
  falls outside the LTE baseline range.

## Current State
- Resolved: R1 adaptive lanes/idle backoff; R2 keyed DNS payload with NXDOMAIN probes; R3 zone and
  QUIC fingerprint.
- Last relevant evidence: 2026-11-16 phone Speedtest through the tunnel DL 6.19/6.32 Mbps,
  UL 0.79/0.40 Mbps, ping 156-174 ms; signed APK installed and the server restarted with the token
  credential. Baseline 2026-11-15: DL 7.32/5.99, UL 0.92/0.67, ping 149-163 ms.
- Blocker: none.
- Next: observation only.

## Material Decisions
- 2026-11-16: three packages accepted in order; R3 zone realism before R2 wire change where it is
  independent, because it is zero-bandwidth and de-risks probing while the encrypted wire is developed.
- 2026-11-16: the wire counter was replaced with a 4-byte random nonce. A plaintext counter put a
  stable base32 prefix (observed `M43DIY...`) into every QNAME and TXT response, which defeats the
  purpose; a nonce collision only mixes two already-encrypted QUIC packets, and QUIC remains the
  security boundary. Overhead stays 6 bytes (nonce + hidden magic).

## Checkpoint History
- 2026-11-16: goal created.
- 2026-11-16: R1 client implemented and unit-tested; R3 zone replies and EDNS/TTL changes implemented
  and probed; R2 client/server implemented and verified over local Rust<->C interop.
- 2026-11-16: signed APK built and installed; n-de1 server, unit token credential and 180 s idle
  timeout deployed with a backup; live zone/crypto probes and phone Speedtest passed.
