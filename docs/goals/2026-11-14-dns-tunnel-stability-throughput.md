# Goal: DNS tunnel stability and DL/UL throughput (Yandex / Rostelecom / LTE-operator DNS)

Status: active
Source: user instruction 2026-11-14 (RECON plan accepted: "делай копию плана в цель и начинай итеративную реализацию"); resolver scope restricted the same day.
Last updated: 2026-11-15

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
    - User-reported browsing failure reproduced over ADB after deployment. Initially debug VPN was
      active and both DNS lookup and literal-IP HTTPS timed out. Force-stopping both app variants and
      starting a fresh session restored access; the original stuck-session cause is not established.
      Installed the signed native-bridge release over production (same signer, versionCode 1859),
      stopped debug VPN, and verified `warp=on`. Brave rendered Wikipedia successfully.
    - Sustained signed-release phone check: 48 requests (12 rounds × Cloudflare/Google/Wikipedia/Yandex),
      12 connection/handshake timeouts at 8 s; all 12 Cloudflare requests passed. Other successful
      requests sometimes needed 3–6 s. Local aggregate evidence:
      `/home/stfu/.cache/opencode-tmp/opencode/owenclave-sites-20261114.jsonl`.
      The same Google destination IP also timed out without the phone/tunnel: host direct 3/3 passed,
      WARP SOCKS 2/3 timed out in one paired window. Six later paired native-netns/WARP-SOCKS checks
      each had one 6 s TLS timeout; another SOCKS success took 5.08 s. Native netns requests explicitly
      bound to CloudflareWARP. This proves a reachable egress failure independent of the DNS carrier,
      but not its exact WARP root cause or attribution of the initial fully stuck session.
    - User approved one WARP disconnect/connect, then explicitly replaced the egress requirement:
      permanently use n-de1 DIRECT, without WARP. Reconnect did not fix TLS failures; the existing
      phone session became fully stuck (28/28 subsequent probes failed) despite WARP recovery.
    - DIRECT cutover at 2026-11-14 19:38:59 UTC: current FlowRelay built from existing source (Go tests
      passed), host-loopback listener, eth0-bound destination/resolver sockets, deny-private enabled;
      Slipstream target changed to loopback and both units detached from WARP lifecycle. Jazz unchanged.
      After one manual phone VPN restart: exact same 48 website probes passed 48/48, p50/p95/max
      0.693/1.287/2.187 s. Phone public IP matches n-de1, warp=off; TCP DL4MiB 6.38 s, acknowledged
      UL512KiB 8.67 s, UDP DNS 3/3 transaction-checked. Wrong token/private destination rejected on backend.
      Evidence: `/home/stfu/.cache/opencode-tmp/opencode/owenclave-sites-direct-20261114.jsonl`.
      Automatic recovery of the old stuck session remains unresolved; no fresh 30-minute video acceptance.

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
- C6: Latest user decision: DNS Tunnel/FlowRelay egress is permanently n-de1 DIRECT, not WARP;
  no alternate egress fallback. Jazz/WARP remains separate. Preserve token auth and private-destination guard.

### Non-goals
- Providers outside C1; UDP transport to resolvers; MasterDNS, Base36, BBR3 (already rejected).
- Battery quantification; Jitsi goal; CI/release activation.

## Change Envelope
- Target: C server crash fix (`slipstream-c/src/`, its picoquic), native TCP-DNS adapter in
  `slipstream-rust/crates/slipstream-client` (replacing the Kotlin blocking adapter path in
  `SlipstreamInstance.kt`), adapter lane count / pipelining depth, server QUIC MTU.
- Repair extension justified by reproduced R5 starvation: Rust vendor picoquic RESET_STREAM
  receive-credit accounting and the nearest existing FFI backpressure regression test.
- Direct consumers: `owenclave/.../SlipstreamInstance.kt`, `DnsttFmt.kt`, `bin/lib/slipstream/smoke/`.
- Allowed: source, unit tests, smoke harness options, this document. Forbidden: committed secrets,
  generated jniLibs/APKs, new external services.
- Budget: none stated.

## Current Checkpoint
- 2026-11-15: user reports further intermittent timeouts while browsing different domains on fixed APK.
  Preserved active phone session (native PID10231), both server services NRestarts0. Initial literal-IP
  HTTPS passed. Eight-domain load (Cloudflare, Google, Wikipedia, Yandex, GitHub, MDN, Mozilla, Rust):
  192 pages + four4MiB downloads all passed, HTTP200/204. Evidence/harness:
  `/home/stfu/.cache/opencode-tmp/opencode/dns-multidomain-before-20261115.jsonl` and
  `dns-multidomain-20261115.py`. Firefox showed a prior Speedtest connection-reset error; explicit fresh
  navigation subsequently loaded it without VPN restart. Do not attribute that isolated error yet.
  Aggregate backend connections varied about40→10; sampled server FDs stayed below the64-live-stream
  cap, so no admission-limit increase is justified by this observation.
- Separate server defect confirmed with a valid peer RESET_STREAM final-size gap: when the C server
  resets its own sending direction, max_stream_id_bidir_local_computed fails to advance, permanently
  losing one stream credit. Existing `tests/test_protocol.c` regression fails before fix and passes with
  reset consumption advanced to final offset before closure accounting; duplicate reset grants no extra
  credit. This is C-server MAX_STREAMS, distinct from yesterday's Android MAX_DATA fix. First full suite:
  6/7 groups passed; three-/five-path timing assertions failed during concurrent compilation, both passed
  isolated unchanged. Published C-main374b964; Debian SHA3d179bd5fa62b028473f51e175d4b29491f4324ecc803cf114541cfcb0888fef
  deployed2026-11-15 07:42:45UTC with backup `/root/backups/slipstream-reset-credit-20261115T074220Z/`.
  Only Slipstream restarted; unit/drop-in hashes and FlowRelay/WARP PIDs unchanged.
- Deployment exposed another reachable failure: the unchanged phone session failed six literal-IP HTTPS
  attempts after server restart; RA74 reverse listener disappeared too. C sockloop never drained picoquic's
  stateless reply queue, leaving old sessions without the reset notification. New loopback runtime test
  `Parity.test_server_restart_returns_stateless_reset` with persistent reset seed times out before fix,
  passes in~1s after causal per-request stateless DNS replies are sent; fresh payload connection passes.
  Queue is drained immediately for each incoming datagram so responses cannot be assigned to another
  DNS slot. Full ASan/UBSan suite7/7 passed (runtime54.78s). C-main `e513568` deployed07:52:58UTC,
  SHA1beb4f66aa11df1d34e5efdc2289208269d2d863ca3175de9da483add558516a,
  backup `/root/backups/slipstream-stateless-20261115T075236Z/`. RA74 reverse listener recovered,
  but phone session that spent~10min without replies before this deployment did not. Manual VPN restart
  restored it (new native PID15343). Synthetic two-path Rust/C restart passed~0.27s, and native TCP-DNS
  Rust/C bridge restart~1.45s; external harness/logs `dns-rust-c-restart-20261115.py`,
  `dns-rust-c-restart-20261115.log`, `dns-rust-tcp-c-restart-20261115.log` in the external cache.
- Subsequent phone cancellation test STILL FAILED with both server fixes: 54 successful recovery pages,
  then rounds54–56 connection timeouts8s; no concurrent server restart. Evidence
  `dns-cancel-serverfix-20261115.jsonl`. Native PID15343 remained alive at100% one-core CPU; its two
  dns-tcp-bridge threads slept. Server remained active/NRestarts0, only RA74 backend connection present,
  server-direct Wikipedia200/0.298s. This is client-side spinning, not proof of slow egress or 64-stream cap.
  Android native stack capture unavailable on production (`debuggerd: root is required`).
  Local Rust/C cancellation simulations (two native TCP-DNS bridges): 180 rounds passed with BBR/short
  reads; 180 rounds also passed with CUBIC,224lanes/depth1,0.8s cancellation,70ms per-exchange delay.
  Harness/logs `dns-rust-c-cancel-20261115.py`, `dns-rust-c-cancel-dcubic-20261115.log`.
  Debug APK installed separately for native trace, production data untouched. Standalone Android native
  cancellation/recovery isolation is current experiment; restore production VPN when finished.
- Client spin root cause captured with Android lldb on a debuggable build under the same native ELF:
  main thread looped in `picosplay` splay while inserting a stream data node whose parent/left pointed to
  itself; the inserted node was the packet's `received_data`, already queued in that stream's data tree.
  App consumption from inside the QUIC data callback (`picoquic_stream_data_consumed`) could delete the
  stream and empty/recycle its data tree, after which `picoquic_stream_data_callback` deleted the same node
  again. The pool then returned one node twice, giving the self-referential splay node and 100% CPU.
  Fix in Rust vendor picoquic: connection bit `in_stream_data_callback`, set around the data callback;
  `picoquic_stream_data_consumed` skips stream deletion while set; the existing close check after the
  callback deletes it safely. Regression added to `tests/backpressure.rs` with a new C helper: consuming
  the final bytes inside the callback keeps the stream, the next call outside deletes it. Host test passed;
  one unrelated acceptor unit test flaked under parallel container load and passed isolated.
- Phone verification with a fresh native process (no VPN restart during a run):
  diagnostic debuggable APK carrying the same native ELF passed cancellation120/120 and browsing246/246;
  concurrent browsing+cancellation passed 246+120 checks with zero failures. Final non-debuggable APK
  `1d30a8335111b89969de91f1a1497742a58a3b49532579fb603e844dc0976d4f` (ELF
  `2210b0a3d369dcda4c63d952fdc7a2dc05cae06160344438cfef1943eeb31ec5`) installed over production,
  fresh native PID4869, 40/40 cancellation recovery, literal-IP HTTPS200, Wikipedia rendered, DIRECT IP
  `195.128.101.186`, warp=off. Unstripped twin with identical `.text` kept outside Git:
  `/home/stfu/.cache/opencode-tmp/opencode/pair-symbols.so`.
- Deployment caveat: `adb install -r` does not stop an already running VPN child. One apparent post-fix
  production failure reused the previous pre-fix spinning process. Stop VPN/force-stop the app before
  installing and confirm a new native PID; do not attribute such a run to the new APK.
- Residual (not closed): one unexplained spin occurred at cancellation round11 on a fresh fixed process
  (normal APK, PID25096) during this investigation; non-debuggable builds cannot be attached. Later runs,
  including heavy combined load, passed. R5 still needs the frozen 30-minute video session and long browse;
  do not claim universal elimination of browsing timeouts. R1 seven-day crash-free observation continues.
- Closes: server MAX_STREAMS/stateless reply defects and the reproduced client data-callback spin.
- Next: observe the seven-day R1 window and run the 30-minute R5 session on the delivered APK.
  Keep MTU900/DIRECT/resolvers unchanged.
- R1: coredump capture active on n-de1 since 2026-11-14 14:35 CET (drop-in
  `/etc/systemd/system/slipstream.service.d/coredump.conf`, recorded in ops `docs/n-de1.md`).
  Fresh read-only check contradicts the handover's no-core report: SIGABRT at 18:03:01 CET,
  service restarted at 18:03:04, NRestarts=1; `/var/lib/slipstream/core` exists (3,084,288 bytes, 0600).
  On-host stack inspection is blocked: `gdb: not found`. No package installed, core copied or deleted.
  Any next abort leaves `/var/lib/slipstream/core` (0600, contains key material: analyse on host only,
  `gdb` against the unstripped production binary, then delete).
- Remove the drop-in only after seven crash-free days after deployment, with restart approval.
- R5: DIRECT cutover and reset-credit fix passed the bounded phone tests below. Remaining:
  separate network/server-restart recovery qualification and the frozen 30-minute video session.
  No further WARP changes needed for this path; do not change MTU/resolver choice to mask unrelated failures.
- Active repair checkpoint (user reports another full outage on DIRECT): same production phone VPN/native
  child remained alive, while literal-IP HTTPS timed out; server services active with zero restarts.
  A clean phone restart restored traffic. Bounded baseline load then passed 240 page fetches + six 4MiB
  downloads; 20 rounds of three deliberately cancelled downloads + one recovery page also passed recovery.
  Harnesses/evidence outside Git: `dns-browse-load.py`, `dns-load-before-20261114.jsonl`,
  `dns-cancel-load.py`, `dns-cancel-before-20261114.jsonl` under `/home/stfu/.cache/opencode-tmp/opencode/`.
  These initial passes did not invalidate the observed outage; the longer cancellation run below reproduced it.
- Confirmed local defect: Rust vendor picoquic RESET_STREAM charges final size to data_received but
  does not consume discarded bytes, leaking connection receive credit. Existing FFI backpressure test
  extended with valid reset-before-data/repeated-reset case: fails before fix (received8192, consumed not8192),
  passes after releasing the abandoned credit before callbacks. Test command:
  `CC=/usr/bin/clang CXX=/usr/bin/clang++ CARGO_TARGET_DIR=/home/stfu/.cache/opencode-tmp/opencode/ss-reset-credit-target cargo test -p slipstream-ffi --test backpressure --features picoquic-minimal-build`.
  CMake library required explicit rebuild before relink (Cargo does not track vendor source edits).
  FFI package tests passed after the fix. Real-phone before/after evidence follows below.
- Reachable phone reproduction confirmed on the old signed APK: after the ordinary load, 20 cancellation
  rounds and a passing 130-second idle check, a longer cancellation run stalled at zero-based round41
  (Wikipedia body stopped at81911 bytes), then rounds42/43 could not establish any connection.
  Harness stopped after three failed recovery probes. Evidence: `dns-cancel-long-before-20261114.jsonl`.
  Each round has three concurrent 4MiB downloads cancelled at0.8s, one second pause, then a recovery page
  with8s deadline. Server NRestarts stayed0 and direct server Wikipedia control returned200 in0.276s.
  This is the before-fix failure point for the same-session comparison.
- Candidate built with Rust1.97.1/cargo-ndk4.1.2/NDK29 in Debian13 after separating the host picoquic
  test cache from Android libs. ELF SHA-256 `5744e35d7dfca300b875a7e0b910b235ac47803748d98d0dbbf97575fc54cfda`.
  Release APK signed with the existing signer, alignment checked, installed over production via ADB.
  Trial began with native PID17828, direct IP/warp=off verified. Output filenames:
  `dns-load-after-20261114.jsonl`, `dns-cancel-after-20261114.jsonl`,
  `dns-cancel-long-after-20261114.jsonl` in the same external cache directory.
  User chose publishing this mobile workspace to a separate branch of `0FL01/slipstream`, not server main.
- First candidate load result: 245/246 required transfers passed. Google round15 failed with curl35
  during TLS after0.214s, not a timeout; simultaneous pages and all later rounds passed. Native PID17828
  unchanged, literal-IP HTTPS control still200 in0.409s. Error text was not retained by the first harness,
  so attribution is unknown, not dismissed as external. Harness now records bounded curl error text for
  the fixed public test endpoints. No code change was justified solely by that isolated TLS failure.
- Final candidate comparison, SAME native PID17828 throughout, no VPN restart: short cancellation recovery
  20/20, 130s pause, long cancellation recovery120/120 (360 intentionally timed-out downloads), then
  confirming page/load run246/246. HTTP status codes checked (200/204), six load bodies exactly4MiB each.
  Confirming240 pages p50/p95/max0.690/1.323/1.844s; six4MiB downloads7.27–11.30s under concurrent browsing.
  Final direct public IP/warp=off rechecked, Brave rendered Wikipedia. Confirming output:
  `dns-load-confirm-after-20261114.jsonl`. The isolated first-pass TLS error remains a recorded residual;
  do not claim all possible connection failures or the full frozen R5 video gate are resolved.
- Accepted release APK replaces `/home/stfu/Torrents/apk/Owenclave-0.17.50-native-bridge-arm64-v8a.apk`;
  SHA-256 `35bc7273a927c8925dea4f5cefc6192c02162fa6a69a9acd76ba8ae5edd54365`.
  Signing certificate unchanged, v1/v2/v3 and16KiB alignment verified. Installed on production package;
  debug VPN stopped. Server code/config was not changed in this client-fix iteration.

## Current State
- Resolved: R2, R3, R4. R1 and R5 in progress (see evidence). Phone runs the signed stream-delete-fixed
  production APK on DIRECT (PID4869 at handover); debug VPN off. Delivered file
  `/home/stfu/Torrents/apk/Owenclave-0.17.50-native-bridge-arm64-v8a.apk`, SHA256
  `1d30a8335111b89969de91f1a1497742a58a3b49532579fb603e844dc0976d4f`.
  Published workspace branch: `mobile/dns-reset-credit`
  in `0FL01/slipstream` (separate layout/history from the C-server main branch).
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
- Blocker: R1 live acceptance requires the seven-day observation interval. Android TCP/UDP post-cutover
  acceptance now passes on DIRECT; R5 remains incomplete as above.
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
- 2026-11-14 DIRECT checkpoint: user explicitly approved permanent DNS egress change after the WARP
  reconnect failed to fix independent TLS stalls. Ops authority updated after 48/48 phone probes,
  TCP/UDP and private/auth guards passed; signed release installed, debug VPN stopped.
  Current phone left on production VPN/DIRECT. No new APK source change in this checkpoint.

## Completion
- Resolved outcomes:
- Commands and artifacts:
- Constraint and diff-scope check:
- Final status:
