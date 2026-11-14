# Goal: DNS tunnel stability and DL/UL throughput (Yandex / Rostelecom / LTE-operator DNS)

Status: active
Source: user instruction 2026-11-14 (RECON plan accepted: "делай копию плана в цель и начинай итеративную реализацию"); resolver scope restricted the same day.
Last updated: 2026-11-14

## Objective
A stable DNS-tunnel channel usable for video, music and browsing on the lab LTE phone: the authoritative
server no longer crashes, the client recovers from resolver connection churn without stalls, and
Download/Upload through the real APK/gVisor path improve over the current accepted baseline.

## Execution Directive
Complete the frozen Required Outcomes using the listed Change Envelope and Primary Evidence. Work on the
smallest unresolved outcome. Do not add requirements from reviews, tests, tools, speculative risks, or
optional source text. Finish when every required outcome is resolved and affected constraints remain
satisfied.

## RECON baseline (2026-11-14, measured)
- Accepted handset baseline (historical, `owenclave/docs/goals/2026-09-09-yandex-multipath.md`):
  DL 268 kB/s, ACKed UL 23.6 kB/s, loaded 4 KiB p95 0.70 s (two-path Yandex `.88+.1`, 56 workers / 64 queue).
- n-de1 `slipstream-server` (C fork, sha256 `b7c6b7ce…`) exited with SIGABRT 17 times between
  2026-10-31 and 2026-11-13 (`journalctl -u slipstream.service`); every exit drops all QUIC sessions.
  No coredump capture configured.
- Resolver DNS layer from the phone, TCP/53, unique NXDOMAIN names of 253 chars, serial exchanges:
  Yandex `.88` 56 conns ≈ 320–390 QPS, 112 ≈ 700, 224 ≈ 1300, RTT p50 ≈ 145–155 ms flat.
  Throughput is therefore bounded by in-flight exchanges (56 / RTT), not by the resolver.
  Other in-scope resolvers at 56 conns: Yandex `.1` 360 QPS, Rostelecom `195.208.4.1` 400 QPS / p50 112 ms,
  LTE operator `176.59.127.174` 221 QPS with tail up to 3.9 s.
- Yandex TCP pipelining: responses arrive out of order (match by DNS ID); at depth ≥2 connections are
  closed after a median ~12 answers / 1–6 s; depth ≥8 causes mass resets. 112 conns × depth 2 ≈ 1230 QPS.
- LTE TCP connect to resolvers: p50 60–125 ms, p95 up to 1.1 s; Kotlin adapter reconnects inline on the query path.
- Server QUIC MTU is 900 (`slipstream-c/src/slipstream_server_runtime.c:127`); resolver egress to n-de1
  advertises EDNS 1232–4096, no fragmentation observed. Upstream capacity is 145 B per query name.

## Frozen Contract

### Required Outcomes
- R1: Authoritative server abort eliminated.
  - Source: plan P0 "стабильность сервера".
  - Acceptance: root cause of SIGABRT identified and fixed (or server replaced by a non-crashing build);
    zero `status=6/ABRT` (or other crash) in `journalctl -u slipstream.service` for 7 days after deployment.
  - Primary evidence: local reproduction/regression test + 7-day journal check on n-de1.
  - Status: in_progress
  - Evidence: 2026-11-14 resume: downloaded production binary SHA-256
    `b7c6b7ce4905adde4108b820115f1fce538d4afa79782fba6745203e5d2b3702` aborts locally
    on loopback after a single 12-byte DNS header with QDCOUNT=1: exit SIGABRT,
    `codec.c:194: block_okay: Assertion block.size > 0 failed`.
    Local fix permits exhausted decode blocks, rejects malformed OPT lengths, and fixes unsigned
    32-bit decoding shifts. This proves a production crash trigger, not attribution of all historical exits.
    Fresh Clang ASan/UBSan Meson suite: 7/7 passed, including codec, 200,000 deterministic mutations
    plus truncations, and runtime transport/multipath checks (56.65 s).
    Deployed with explicit approval at 2026-11-14 19:02:22 UTC; server-side smoke passed.
    Seven-day crash-free observation remains outstanding (earliest finish 2026-11-21 19:02:22 UTC).
- R2: Client survives resolver connection churn without query loss.
  - Source: plan P0 "стабильность клиента".
  - Acceptance: TCP DNS adapter reconnects off the query path, re-sends queries from a closed connection,
    validates DNS ID/QR; native smoke on the phone shows no upload ACK failures caused by adapter loss.
  - Primary evidence: unit tests for the adapter + phone native smoke window.
  - Status: verified
  - Evidence: bridge unit tests 27/27 (incl. resolver closing every 3 answers, zero loss); Owenclave
    unit tests 33/33; phone native windows (18) and APK/gVisor A/B (12) all transfers passed.
- R3: Download improved.
  - Source: plan P1 "Download (цель ×2–3)".
  - Acceptance: paired-median DL through the real APK/gVisor path ≥ 2× the accepted baseline control
    measured in the same campaign, with all required transfers passing.
  - Primary evidence: six CT/TC pairs through the real APK/gVisor path (see Evidence for harness).
  - Status: verified
  - Evidence: 2026-11-14, six fresh alternating CT/TC pairs, control = installed production APK
    (versionCode 1859, Kotlin 28+28 adapter), treatment = debug APK `org.owenewans.owenclave.debug`
    (native bridge, 112 lanes per path, ELF sha256 `ee86d6cd…`), both `tcp+mp://77.88.8.88:53`,
    egress verified `warp=on` before every window; Termux curl to Cloudflare speed endpoints
    (warmup, 5×4 KiB idle, 4 MiB DL, 512 KiB UL, 4×1 MiB parallel, UL + 5×4 KiB under 4 MiB DL).
    DL 4 MiB paired-median ×3.27 (6/6 wins, ~310 → ~1010 kB/s); 4×1 MiB ×3.43 (6/6);
    loaded DL ×2.33 (6/6); transfers 18/18 both arms.
- R4: Upload improved without hurting DL.
  - Source: plan P1 "Upload".
  - Acceptance: paired-median ACKed UL ≥ 2× control in the same campaign, ACK success not below control.
  - Primary evidence: same campaign as R3.
  - Status: verified
  - Evidence: UL 512 KiB (HTTP 200 after full body) paired-median ×2.06 (6/6, ~35 → ~72 kB/s);
    UL under DL ×2.57 (6/6); ACK success 6/6 + 6/6 both arms. Margin over the 2× bar is small.
    Loaded 4 KiB p50/p95/max: control 0.702/1.621/1.861 s, treatment 0.530/0.692/1.068 s.
- R5: Usable for video/music/browsing.
  - Source: user goal "стабильный канал для просмотра видео и прослушивания музыки + сёрфинг".
  - Acceptance: 30-minute session on the phone (YouTube 480p playback + browser probes) without a
    tunnel-wide stall > 10 s; loaded 4 KiB p95 not worse than control.
  - Primary evidence: foreground phone session log (aggregate only).
  - Status: in_progress
  - Evidence (not yet passing):
    - Session A (2026-11-14 17:47, debug APK, `.88+.1`, static-image 2 h video at ReVanced default 144p):
      876 probes 4 KiB/2 s: 2 timeouts, p50/p95/p99 0.473/0.645/0.875 s, worst success gap 12.1 s;
      30/30 Wikipedia page loads, p50 1.1 s, max 16.9 s; playback continuous (20:11 played in ~21 min,
      then YouTube "Continue watching?" pause).
    - Separate 480p60 check (Big Buck Bunny): 5 min PLAYING, no BUFFERING, buffer ahead of position.
    - Session B (18:26, 480p real videos in a mix): 834 probes, 1 timeout, p50/p95/p99 0.481/0.698/1.398 s,
      worst gap 12.7 s; pages 29/30 ok, p50 1.47 s, 10 of 30 took 10–15 s; two ~45–60 s BUFFERING
      episodes at mix transitions. Parallel n-de1 WARP-only probe (789 probes) failed at two other
      moments, not at the phone failure time, so a WARP-wide outage is not the cause of that gap.
      Manual next-video starts at 144p: 2.8–4.4 s.
    - Gap > 10 s criterion is missed by 2.1–2.7 s; cause of 10 s probe timeouts, slow page loads under
      480p load and transition buffering was not identified in these sessions.
    - Later ops evidence (`/home/stfu/ai/work-table/space/docs/n-de1.md`): WireGuard/rekey-period
      delays of 1.5–1.8 s reproduced through WARP without the tunnel (3/277 vs direct 0/286).
      MASQUE removed Cloudflare stalls but introduced 7–8 s TLS delays to non-Cloudflare destinations;
      WireGuard was restored. Handover attributes the longer stalls to WARP, but this record does not
      prove that every earlier phone gap or buffering episode had that cause. R5 acceptance remains open;
      do not change the tunnel or WARP based solely on that attribution.

### Constraints
- C1: Resolvers in scope: Yandex (`77.88.8.88`, `77.88.8.1`, other Yandex IPv4), Rostelecom
  (`195.208.4.1`, `195.208.5.1`) and the LTE operator's DNS. No other providers are tested or added.
- C2: Root and `owenclave/AGENTS.md` rules: stdin-only secrets, one QUIC stream per flow, gVisor only,
  no fallback/mux, explicit multipath stays explicit, pinned certificate.
- C3: n-de1 is shared with RA74. Any server binary/config change or restart needs explicit user approval
  for that action; read-only inspection is allowed.
- C4: Load on third-party resolvers stays at production-like levels (≤ the tunnel's own lane budget,
  short foreground windows). No stress sweeps.
- C5: No tokens, payload or QNAME captures, configs or generated binaries in Git.

### Non-goals
- Providers outside C1; UDP transport to resolvers; MasterDNS, Base36, BBR3 (already rejected).
- Battery quantification; Jitsi goal; CI/release activation.

## Change Envelope
- Target: C server crash fix (`slipstream-c/src/`, its picoquic), native TCP-DNS adapter in
  `slipstream-rust/crates/slipstream-client` (replacing the Kotlin blocking adapter path in
  `SlipstreamInstance.kt`), adapter lane count / pipelining depth, server QUIC MTU.
- Direct consumers: `owenclave/.../SlipstreamInstance.kt`, `DnsttFmt.kt`, `bin/lib/slipstream/smoke/`.
- Allowed: source, unit tests, smoke harness options, this document. Forbidden: committed secrets,
  generated jniLibs/APKs, new external services.
- Budget: none stated.

## Current Checkpoint
- Closes: R1 (locally reproduced production abort and candidate fix).
- Next: observe seven crash-free days from 2026-11-14 19:02:22 UTC, then request drop-in removal/restart.
  Deployment retained default MTU 900; Yandex 900→1200 A/B is a subsequent approved experiment.
- R1: coredump capture active on n-de1 since 2026-11-14 14:35 CET (drop-in
  `/etc/systemd/system/slipstream.service.d/coredump.conf`, recorded in ops `docs/n-de1.md`).
  Fresh read-only check contradicts the handover's no-core report: SIGABRT at 18:03:01 CET,
  service restarted at 18:03:04, NRestarts=1; `/var/lib/slipstream/core` exists (3,084,288 bytes, 0600).
  On-host stack inspection is blocked: `gdb: not found`. No package installed, core copied or deleted.
  Any next abort leaves `/var/lib/slipstream/core` (0600, contains key material: analyse on host only,
  `gdb` against the unstripped production binary, then delete).
- Remove the drop-in only after seven crash-free days after deployment, with restart approval.
- R5: WARP mitigation belongs to operations; no WARP changes authorized by this source checkpoint.

## Current State
- Resolved: R2, R3, R4. R1 and R5 in progress (see evidence). Handover reports debug VPN running;
  current phone state has not been rechecked.
- Lab setup (reproducible): debug APK `org.owenewans.owenclave.debug` installs beside production
  (debug signing; profile `lab`, `tcp+mp://77.88.8.88:53`). Native ELF built in `rust:1.97.1-bookworm`
  with NDK 29.0.14206865 via `owenclave/bin/lib/slipstream/build.sh` (host lacks Perl FindBin);
  core AAR via `./run lib core` on host. Flow token copy: `~/.config/owenclave-lab/flow-token.bin` (0600).
- Phone native screen (Termux, same ELF, single window each, Cloudflare endpoints), DL/UL kB/s:
  Yandex `.88` 56×1 323/37 (×2 windows); 112×1 603–626/56; 56×2 340–589/51–59 (pipelining unstable);
  168×1 824–864/61–72; 112×2 962/68; `.88+.1` 224×1 967–1096/63–83;
  Rostelecom `195.208.4.1` 112×1 642–737/60–67 (lowest latency); LTE operator DNS 112×1 458/43–47;
  Rostelecom pair 224 1000/82; Yandex `.88`+Rostelecom 224 1211/89 (best, single window).
  Server CPU at ~1 MB/s: 13–26 % of one vCPU, no throttling.
- R1 local evidence: ASan/UBSan stress 4×900 s, 4.6 M queries, no abort.
- Blocker: R1 live acceptance requires the seven-day observation interval; Android TCP/UDP post-deploy
  acceptance has not been rerun in this checkpoint.
- Debian 12 x86_64 release artifact (outside Git):
  `/home/stfu/.cache/opencode-tmp/opencode/ss-resume-debian/build/slipstream-server`, SHA-256
  `75c35137cea0d2902a7307d448a4f40dc83461f23ae49bd76812e5d724e50c50`.
  Built in `debian:bookworm-slim` with static project libraries; system OpenSSL/glibc remain dynamic.
  Local smoke: survived the production-aborting header, answered the next DNS query;
  `--mtu 1200` starts, 599/1401 rejected. Default remains 900; no resolver MTU A/B run yet.
- Deployed artifact hash matches the candidate; Slipstream active, NRestarts=0 after one approved restart.
  Both authoritative suffixes answer DNS; FlowRelay/WARP PID/counters and unit/drop-in hashes unchanged.
  Exact backup/rollback anchor is recorded in the authoritative ops `docs/n-de1.md`.
- Next: seven-day observation and separately approved Yandex MTU A/B.

## Material Decisions
- 2026-11-14: resolver scope limited to Yandex, Rostelecom, LTE operator DNS (user).
- 2026-11-14: user approved n-de1 coredump drop-in + restart, lab token use, Android build setup.
- 2026-11-14: APK default bridge budget 112 lanes / 128 queue per TCP path, depth 1 (pipelining
  less stable under Yandex connection churn). Kotlin `DnsTcpAdapter` left in source unused, pending acceptance.
- 2026-11-14: this goal is independent of the active `owenclave/docs/goals/2026-09-09-multipath-throughput.md`; it does not close it.

## Checkpoint History
- 2026-11-14: RECON, baseline measurements recorded above.
- 2026-11-14: R1 local ASan stress (4×900 s) — no abort; next is production coredump (needs approval).
- 2026-11-14: R2 native TCP bridge — retry policy fixed so resolver-side connection churn does not
  consume attempts (initial version lost 35–38% of queries under churn); tests pass.
- 2026-11-14: R1 coredump capture enabled on n-de1 (verified with a sandboxed SIGABRT test process).
- 2026-11-14: R3/R4 verified — six APK/gVisor CT/TC pairs, DL ×3.27, UL ×2.06, loaded p95 1.62 → 0.69 s.
- 2026-11-14: R5 two 30-min sessions — throughput/latency fine, 480p60 plays; strict >10 s gap criterion
  missed (12.1/12.7 s), slow page loads and transition buffering under 480p load remain unexplained.
- 2026-11-14 resume: exact production binary reproduced header-only SIGABRT locally; candidate passed
  `ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 meson test -C
  /home/stfu/.cache/opencode-tmp/opencode/ss-resume-clang --print-errorlogs` (7/7).
  Host GCC sanitizer setup failed because libasan/libubsan are absent; Clang build succeeded.
  Debian 12 release built and passed local crash-regression/CLI smoke. Independent agent review failed
  to run; direct review checked CLI consumers, slot initialization and picoquic packet-size boundary.
  No remote writes or restarts performed in this checkpoint.
- 2026-11-14 deployment checkpoint: user explicitly approved binary replacement and Slipstream-only
  restart. Deployed `eda3e1f` at 19:02:22 UTC with current-C rollback backup, default MTU 900.
  Server-side DNS/listener checks passed; no Android TCP/UDP end-to-end or MTU A/B claim.

## Completion
- Resolved outcomes:
- Commands and artifacts:
- Constraint and diff-scope check:
- Final status:
