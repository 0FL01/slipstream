# Goal: make the DNS tunnel less distinguishable from legitimate DNS

Status: active
Source: user instruction 2026-11-16 (accepts Phase 0 and Phase 1 of the legit-DNS plan;
Phase 2 is explicitly out of scope).
Last updated: 2026-11-16

## Objective
Reduce static DPI fingerprints of the DNS tunnel without losing throughput. Add aggregate-only
measurement of the current wire shape and a legit stub-DNS baseline from the lab phone, then fix
the cheap artifacts: sequential DNS IDs, fixed label segmentation, uppercase base32, missing
zone records, and the uniform TXT TTL. Phase 2 (base64 TXT, ~25% downstream cost) is not funded.

## Execution Directive
Complete the frozen Required Outcomes using the listed Change Envelope and Primary Evidence. Work on
the smallest unresolved outcome. Do not add requirements from reviews, tests, tools, speculative risks,
or optional source text. Finish when every required outcome is resolved and affected constraints remain
satisfied.

## Frozen Contract

### Required Outcomes
- L0: Aggregate DPI-eye measurement and classifier.
  - Source: Phase 0.
  - Acceptance: client and phone legit-stub counters expose, without QNAMEs or payloads, QTYPE mix,
    QNAME length buckets and p50/p95, label length buckets and 56-58 share, ID delta=1 share, binary
    TXT share, response size buckets, qps, active TCP/53 lanes, and unique QNAMEs/min; a classifier
    reports H1-H8 for idle, browse, download-only, upload-only and Speedtest, plus the phone legit
    baseline.
  - Primary evidence: one stats line per interval in logcat for the tunnel and the phone DNS adapter,
    plus a table of H1-H8 with weights.
  - Status: tunnel part done; phone legit baseline blocked.
  - Evidence: `dpi-stats` from the installed APK covers idle and Speedtest. Idle: 10-12 queries/10 s,
    qps ~1, QNAME p50 118/p95 153-205, label 56-58 share 6.7-7.4%, ID delta=1 0%, binary TXT 100%,
    lanes 17. Speedtest load: up to 10,118 queries/10 s (qps 1,012), QNAME p50 118 during download and
    247 during upload, label 56-58 share 5.2-5.6%, ID delta=1 ~0%, lanes 146-168, binary TXT 100%.
    H1/H2/H4/H5/H7/H8 fire; H3 and H6 are off. `scripts/dpi_stats.py` prints the table.
    Phone legit baseline is blocked: the pinned Go core resolves `[dns-out]` internally and does not
    call the Kotlin `LocalResolver.exchange`/`lookupIP` callbacks; the old `DnsTcpAdapter` is unused,
    and system DNS cannot be captured without root. Instrumenting the core or a host reference is a
    separate decision.
- L1: Static artifact removal.
  - Source: Phase 1.
  - Acceptance: DNS IDs are a full-period 16-bit permutation instead of +1; labels are randomly
    segmented in 20..57 chars; base32 is lowercase (C decoder accepts it); the zone serves a coherent
    null MX, apex SPF TXT and `_dmarc` reject TXT (no fake WWW/CAA); TXT TTL comes from a realistic set;
    cargo tests, C suite, local interop and phone throughput stay green.
  - Primary evidence: unit/vector/interop tests, zone probe matrix, before/after H-ID and H-label,
    Speedtest A/B.
  - Status: done.
  - Evidence: DNS IDs use a full-period 16-bit LCG (`dns/id.rs`); base32 is lowercase and labels are
    20..=57 characters (`dots.rs`, `lib.rs`); the C decoder accepted lowercase and variable dots in
    the local Rust<->C SOCKS transfer. Zone live: `MX 0 .`, `TXT "v=spf1 -all"`,
    `_dmarc TXT "v=DMARC1; p=reject"`, unknown names still NXDOMAIN+SOA; TXT TTL set is 30/60/120/300
    plus 0-2 jitter. `cargo test` 30/30, C suite 7/7, Kotlin compile, signed APK; phone H3/H6 are off.
    Server binary `1a610a3d…` deployed with backup `/root/backups/slipstream-legit-20261116T123312Z/`.

### Constraints
- C1: Keep existing contracts: FlowRelay token auth, one QUIC stream per app flow, gVisor only,
  no mux/fallback, certificate pinning, DIRECT egress.
- C2: No new dependency or service; stdlib/OpenSSL/existing crates first.
- C3: Do not log QNAMEs, payloads, tokens or keys; only aggregate counters. The stats line is
  aggregate-only and safe for production logcat.
- C4: Phase 2 (base64 TXT) and cover traffic are out of scope.
- C5: Production n-de1 changes need a backup and one short restart; deploy client and server together.

### Non-goals
- Full statistical/behavioral masquerade at 6 Mbps; DoH/transport change; domain renaming;
  fake services (WWW/CAA/MX targets that do not exist).

## Change Envelope
- Client: `slipstream-rust/crates/slipstream-client` (stats, DNS ID, qname building),
  `slipstream-rust/crates/slipstream-dns` (base32/dots).
- Server: `slipstream-c/src/slipstream_server.c` (zone records, TTL).
- App: `owenclave/.../SlipstreamInstance.kt` (pass `--dpi-stats`, log aggregate lines, legit DNS
  adapter counters).
- Allowed artifacts: source, tests, vectors, goal/ops docs, temporary measurement logs outside Git.
- Forbidden: secrets/payload/QNAME logs, new public endpoints, disabling auth.

## Current Checkpoint
- Closes: L1 and the tunnel half of L0.
- Smallest next action: decide whether to instrument the pinned Go core for the phone legit DNS
  baseline, or accept a host/synthetic reference; do not implement Phase 2.
- Expected evidence: a `source=legit` line from the phone or an explicit baseline waiver.
- Stop or replan if: core instrumentation grows beyond the measurement scope.

## Current State
- Resolved: L1; L0 tunnel metrics.
- Last relevant evidence: phone Speedtest 10,118 queries/10 s, QNAME p50 118/247, label 56-58 5.2-5.6%,
  ID delta=1 0%, lanes up to 168, H3/H6 off; H1/H2/H4/H5/H7/H8 remain.
- Blocker: phone legit baseline; the Go core resolves DNS internally.
- Next: baseline decision.

## Material Decisions
- 2026-11-16: Phase 2 (base64 TXT) rejected because the ~25% downstream cost is not acceptable.
- 2026-11-16: the legit stub baseline is measured through the app's DNS adapter because the phone
  is not rooted and system DNS cannot be captured with VPN off; the adapter carries the same apps'
  real DNS queries.
- 2026-11-16: binary TXT and the high qps/QNAME length remain residual risks; this goal only removes
  static artifacts, not the volume signature.
- 2026-11-16: the phone legit baseline via Kotlin callbacks is not reachable because the pinned Go
  core owns `dns-out`; root or core instrumentation is required. The classifier script and
  `source=legit` format are ready for that follow-up.

## Checkpoint History
- 2026-11-16: goal created from the accepted Phase 0/Phase 1 plan.
- 2026-11-16: L1 implemented and deployed; tunnel H1-H8 measured on the phone; phone legit baseline
  blocked by the Go core DNS path.
