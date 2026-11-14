# Goal: migrate DNS Tunnel to MasterDnsVPN

Status: complete (migration evaluated; subsequent user-requested Slipstream rollback verified)
Source: user request of 2026-09-07 to deploy MasterDnsVPN, integrate Owenclave,
run before/after comparisons and explain the result.
Last updated: 2026-09-07

## Objective and execution

Deliver a verified MasterDnsVPN DNS path through Owenclave, with measured LTE
comparison against the accepted Slipstream build. Complete the outcomes below;
do not treat an upstream benchmark or an insecure configuration as acceptance.

## Required outcomes

- R1: establish a safe, reproducible integration boundary.
  - Acceptance: pinned source, authenticated encryption of application streams,
    stdin-only Android secrets, one resolver/child and private WARP-only FlowRelay.
  - Evidence: focused source review, build and boundary tests.
   - Status: verified; pinned build, TLS/authentication tests and unchanged artifact hashes at closure.
- R2: deploy the candidate and integrate the real Owenclave gVisor path.
  - Acceptance: automatic/manual startup, exact TCP payload, application UDP,
    expected WARP egress, cancellation/recovery and no retained traffic logs.
  - Evidence: APK/device acceptance and owning-node service/listener checks.
   - Status: verified; device TCP/UDP/automatic/manual/lifecycle and DE service checks below.
- R3: compare and document before/after behavior and all material failures.
  - Acceptance: order-balanced paired upload/download and loaded latency, exact
    completed payloads distinguished from deliberate cancellations; explain
    measured gains or regressions without promising a universal speedup.
  - Evidence: aggregate results here, stable Android and operations documentation.
   - Status: verified; six fresh balanced pairs and stable documentation updated.

## Constraints and change envelope

- Preserve gVisor capture, strict single resolver, sequential startup deadlines
  (VPN 15 seconds; latency 5 seconds), independent application streams, local SOCKS
  authentication and FlowRelay token authentication. No direct fallback or multipath.
- Preserve payload confidentiality, integrity and forward secrecy. Do not assume
  upstream's encryption setting protects both directions.
- Preserve foreground-only benchmark ownership and bounded idle carrier activity.
- Allowed: pinned native carrier integration under `bin/lib/masterdns/`, the DNS
  sidecar and direct consumers, focused tests, private server TLS boundary and
  owning-node deployment/docs. Keep Slipstream binary/APK/service rollback anchors.
- No unrelated routing changes, public recursive service, host-wide WARP,
  persistent traffic logs, secrets in Git/tool output, or automatic installer.
- The migration supersedes vendor-specific QUIC implementation details, not the
  existing protection, isolation or lifecycle guarantees.
- No commit/push requested for this new task.

## Current checkpoint

Current runtime is Slipstream again, following the subsequent explicit user rollback
request. Server/APK/source rollback and fresh phone TCP/UDP payload verification
are complete. The migration results below are historical, not current deployment.

User subsequently explicitly allowed downtime and disruptive experiments; routine
availability rollbacks between diagnostics are no longer necessary. Security and
egress restrictions are unchanged.

## Decisions and evidence

- 2026-09-07: the stock source is not security-equivalent to Slipstream.
  `internal/dnsparser/transport.go:256-286` builds downstream TXT answers from raw
  frames without encryption; the corresponding client parser does not authenticate
  them. `internal/security/codec.go:243-298` uses unauthenticated ChaCha20, not
  ChaCha20-Poly1305. AES-GCM applies to client queries but does not fix responses.
  No exploitation or live failure injection was performed.
- Implemented substrate: MasterDnsVPN fixed-target TCP transport, with mandatory
  pinned TLS 1.3 per independent application stream before FlowRelay OPEN/token or
  payload. MasterDnsVPN remains an untrusted carrier; inner TLS does not make its
  outer control messages authenticated or hide DNS carrier metadata. No plaintext
  compatibility fallback is acceptable. The legacy bundled certificate has no SAN;
  Go TLS uses mandatory exact DER pinning plus validity checks rather than the host
  CA store or permissive hostname verification. A valid TLS handshake still proves
  possession of its private key. Focused tests cover wrong/expired pins, malformed
  trust, TLS 1.2 rejection, missing local authentication and exact TCP/UoT framing.
- Accepted comparison anchor: Owenclave `b53546d`, 48 serial TCP DNS workers and
  a separate 64-entry queue; `build/dns-performance/h6.apk`, SHA-256
  `965897285383ed942fc982a57943b2b90e559f49ac3aeaccb9fc6d4de3819047`.
  Existing accepted LTE numbers are historical, not a fresh migration baseline.

## Deployment and experiment ledger

- Build: `bash bin/lib/masterdns/build.sh`, pinned standard Go 1.26.5; upstream
  client/server/config tests and new `flowbridge` tests pass. The first toolchain
  check rejected the system `go1.26.5-X:nodwarf5`; the standard Go toolchain was
  downloaded and selected explicitly. The first compile exposed an upstream
  package-local `max(int,int)` shadowing the Go builtin; explicit int64 comparison
  fixed that integration error. No tests were disabled.
- Release APK: `ANDROID_HOME=/home/stfu/Android/Sdk ./gradlew
  :app:assembleOssRelease --console=plain` passed. The candidate reuses profile
  persistence, stdin bootstrap, sequential resolver ownership and the 48/64 TCP
  adapter; the sidecar now parses a structured readiness event.
- DE additions: `/usr/local/bin/masterdns-server`, `/usr/local/bin/flowtls`,
  `/etc/masterdns/server.toml`, `/etc/systemd/system/masterdns.service`,
  `/etc/systemd/system/flowtls.service`. Existing Slipstream/FlowRelay units and
  nftables were backed up under `/etc/masterdns/backups/20260907T142905Z` and left
  unchanged. New services were not enabled at boot during the experiment;
  final enablement is recorded below.
- Private `flowtls` listens at `10.200.0.2:40002` inside `warpns`, terminates TLS 1.3
  with ALPN `owenclave-flowrelay/1`, and only forwards to `10.200.0.2:40001`.
  Its cgroup IP policy permits only `10.200.0.0/30`; it has zero capabilities and
  null output sinks. `flowd` retains token validation and WARP-bound sockets.
- Native `-check` and `systemd-analyze verify` passed before startup. The immediate
  first TLS probe raced `Type=simple` listener startup and got connection refused.
  A subsequent socket check and `openssl s_client` proved TLS 1.3, valid certificate
  and X25519; `flowtls` had zero restarts. No logs were enabled.
- M0 first LTE screen, 2026-09-07 14:31–14:38 UTC: Wi-Fi off, mobile data on,
  Termux captured by Owenclave, manual TCP `77.88.8.8:53`, DE/WARP in both variants.
  Direct external LTE was reachable in the preceding task; this is not proof of
  traversing a currently restricted LTE network.
  - Control: exact 1 MiB down in 8.271227 s (**126.774 kB/s**); exact 128 KiB upload
    with final HTTP 200 in 10.968744 s (**11.950 kB/s**). All four concurrent 1 MiB
    transfers completed within the common 18 s deadline; recovery 0.545586 s.
  - M0: only 783872/1048576 download bytes by 75 s, therefore **failed**, not goodput.
    Upload completed in 25.009089 s (**5.241 kB/s**); idle 4 KiB requests took
    5.215109/5.265610/6.608775 s; recovery 6.059469 s. All four bulk transfers were
    incomplete when cancelled at 18 s. One child before stop, zero afterward.
  - Decision: reject M0 performance, retain its functional evidence only. Results
    are in ignored `build/masterdns/{control,treatment}-screen.txt`; the candidate
    APK is `build/masterdns/artifacts/candidate.apk`. This is a screening pair,
    not the planned order-balanced confirmation series.
- Restoring Slipstream stopped MasterDnsVPN as intended, but exposed exit-code 1
  for a normal cancellation (zero runtime restarts). Source now distinguishes
  owner cancellation from failure; subsequent intentional stops verified Result=success
  and zero runtime restarts.
- Source diagnosis: the Go code default is download MTU 500 and duplication 2;
  upstream's example instead uses maximum download MTU 4000 and duplication 3.
  M0 deliberately had one copy and MTU 500. The dispatcher wakes on work signals,
  so its 50 ms *idle* timer is not evidence of a fixed 20-packet/s throughput cap.
  MTU, query supply and TLS handshake contribution remain hypotheses, not causes
  established by the failed LTE screen.

## Subsequent diagnostic checkpoints

All rates below are decimal kB/s. These are sequential screens, not paired causal
estimates. Same manual TCP resolver and DE/WARP payload path; exact 1 MiB download
and 128 KiB upload unless stated. Upload means all bytes sent followed by HTTP 200,
not an independent echoed checksum. Partial 18-second bulk transfers are failures
for useful-goodput accounting, deliberate cancellations for recovery testing.

| Candidate | Change | Download | Upload | Decision/evidence |
|---|---|---:|---:|---|
| M1 | Download MTU ceiling 500 → 1000 | 15.398 | 6.636 | Negotiated up=109/down=1000/safe=81; still slow |
| M2 | Configured ARQ window 64 | 11.640 (256 KiB) | 5.493 | Rejected; source subsequently proved constructor clamps window to at least 300, so this did **not** test actual window 64 |
| M3 | Restore config 600; Busy ≤8 polls/50 ms, fresh pending target 16 | 92.200 | 6.685 | Query supply materially improved download; upload still slow |
| M4 | Limit actual local send admission to 48, retain receive window | 85.560 | 16.629 | Upload improved; post-bulk recovery had a 15.361 s outlier |
| M5 | Busy ≤16 polls/50 ms, fresh pending target 32 | 150.921 | failed | First screen had upload and one small-request curl TLS failures; remaining loaded requests passed. Five subsequent uploads all passed, 16.021–16.878 kB/s; five small requests 1.216–1.381 s. Failure cause not established; not erased from results |
| M6 | Send admission 48 → 80 | 148.445 | 16.666 | No useful upload gain over M5; restore narrower 48. Recovery 1.214 s |

- Source `internal/arq/arq.go` clamps configured window to 300 and normally admits
  80% of the window. The new managed-only limit reduces send admission without
  changing receive-window or generic constructor semantics. Focused tests pass.
- Polling counts fresh pending observations plus queued work, stops speculative
  supply at the target, and bounds each Busy batch. Warm remains 400 ms, quiet 2 s,
  empty only a 5 s session keepalive; Busy keepalive remains 400 ms. Old observations
  expire from the polling budget after 1 s, preventing lost replies from permanently
  starving downstream supply. It is not a new resolver, path or application stream.
- All M1–M6 native tests/builds, APK builds and installations passed. Artifacts and
  safe aggregate outputs are under ignored `build/masterdns/`; no traffic logs or
  destination metadata were enabled. Temporary negotiated-MTU Android tag removed
  before clean confirmation.
- M1 server cancellation fix deployed with backup
  `/etc/masterdns/backups/20260907T144741Z`; masterdns/flowtls had zero runtime
  restarts and flowd retained its pre-existing count 1. MasterDNS CPU was 2.63 s over
  about 117 s with zero cgroup throttling; removing CPU limits lacks evidence.

## Clean paired confirmation

2026-09-07, approximately 15:24–15:39 UTC. Six fresh pairs, alternating CT/TC;
screening runs excluded. C is the accepted Slipstream 48/64 APK; T is clean
MasterDNS with send admission 48, Busy batch 16/50 ms and fresh target 32,
download MTU ceiling 1000. Same manual TCP resolver `77.88.8.8:53`, gVisor-captured
Termux on LTE, Wi-Fi off, DE/WARP confirmed each window. Neither APK retains
temporary instrumentation. Rates include complete request time, not just body time.

Each window: 128 KiB warm-up, exact 1 MiB download, exact 128 KiB upload followed
by HTTP 200, three idle 4 KiB requests, four concurrent 1 MiB downloads bounded to
18 seconds with five loaded 4 KiB requests, then recovery. Upload endpoint returns
an empty response: HTTP completion is evidence, not an independent echoed checksum.
Cancelled partial bulk bytes are not counted as successful useful goodput.

| Pair/order | C down kB/s | T down kB/s | C up kB/s | T up kB/s |
|---|---:|---:|---:|---:|
| 1 CT | 177.838 | 128.515 | 13.642 | 16.228 |
| 2 TC | 165.200 | 120.577 | 11.134 | 16.442 |
| 3 CT | 183.745 | 143.097 | 17.464 | 16.519 |
| 4 TC | 142.810 | 152.266 | 15.941 | 16.622 |
| 5 CT | 169.435 | 145.142 | 12.170 | 14.697 |
| 6 TC | 153.247 | 151.048 | 11.039 | 16.377 |

- Paired median relative change: **download -18.2296%; upload +19.8581%**.
  This is not a 20% acceptance pass rounded upward, nor a universal speedup.
- Unpaired medians C/T: download 167.317/144.120 kB/s; upload 12.906/16.410 kB/s.
  Ratios of these medians differ from the paired estimator.
- Idle 4 KiB median C/T: 0.574694/1.279841 s; nearest-rank p95 over 18 each:
  1.063112/1.786706 s.
- Loaded 4 KiB median C/T: 0.639988/1.446454 s; nearest-rank p95 over 30 each:
  **0.912086/2.593418 s** (about +184%).
- Recovery median C/T: 0.509559/1.267068 s; p95 over six each:
  0.583668/1.427869 s.
- Required non-bulk failures: zero in both variants. One child before stop,
  zero afterward in all twelve windows. Safe aggregate files:
  ignored `build/masterdns/pair-{1..6}-{C,T}.txt`.

Decision: complete the explicitly requested migration with its measured trade-off,
not claim that upload improvement outweighs download/latency regressions. The
screening evidence supports bounded query supply and send admission as useful
repairs to this integration, not a proven explanation for every remaining delay.

## Final acceptance and deployment

- Automatic: exact 1 MiB down 7.423861 s; 128 KiB up 8.282130 s; application UDP
  STUN passed; DE/WARP; one child then zero after UI stop.
  First automatic harness attempt incorrectly assumed child presence + 5 seconds
  meant readiness. It failed HTTP and immediate stop. Corrected test waits for
  actual connected UI before payload; no application deadline was relaxed.
  Observed 15.824 s UI interval includes XML/tap/poll overhead, not a measurement
  of the internal 15-second aggregate readiness deadline.
- Manual TCP: exact 1 MiB down 12.366837 s (6.352 s TTFB outlier), 128 KiB up
  12.111707 s; application UDP and DE/WARP passed; stop left zero children.
- Manual UDP resolver: exact 128 KiB down 5.321520 s, 32 KiB up 8.766784 s;
  application UDP and DE/WARP passed. Functional, much slower than TCP; not an
  additional performance win. Manual TCP was restored and its UI field verified.
- UDP proof was an application STUN transaction (20-byte request, valid 32-byte
  response and matching transaction ID), not an ordinary DNS lookup. Its mapped
  address matched a fresh DE `warpns` control bound to `CloudflareWARP`; only a
  salted address hash was compared, no exit IP retained in this document.
- UI Stop interrupted an 8 MiB download at 570743 bytes/4.386443 s, curl 56,
  child count zero. Restarted 4 KiB payload passed in 1.287467 s with DE/WARP.
  After 30 seconds screen-off, 4 KiB passed in 1.274338 s. Final owner force-stop
  left zero children; it is not described as a post-lockscreen UI-stop test.
- Initial UI field replacement failed to clear the resolver; bounded clearing of
  the known resolver input followed by exact field assertion fixed the test harness.
  No profile databases, token fields or security unlock mechanism were accessed.
- Final clean APK remains installed; profile is manual TCP, VPN stopped. Device:
  `ZY22JFJ5LP`. Direct external LTE was reachable, so restricted-LTE traversal is
  **not** established. No extra claim about underlay-change recovery was added.
- Final DE backup anchors: `/etc/masterdns/backups/20260907T152413Z` (binary
  update), `/etc/masterdns/backups/20260907T154658Z` (pre-enablement metadata/units).
  `masterdns.service` and `flowtls.service` active/enabled, `NRestarts=0`;
  unchanged `flowd.service` active/enabled, pre-existing `NRestarts=1` stable.
  Slipstream inactive/disabled and retained with old APK as paired rollback.
  Deliberate service switches reset some counters; they are not compared to old
  pre-experiment totals. Native check, unit verify and nftables check passed.
  Public UDP :53 and private :40002/:40001 present; no private :41924;
  all three output sinks null, host-wide WARP remains masked.

## Closure checks and artifacts

- `bash bin/lib/masterdns/build.sh`: focused ordinary Go tests and all three native
  builds pass. `git apply --check managed.patch` against a pristine pinned worktree
  passes; rebuilding reproduces the same artifact hashes.
- `:app:compileOssReleaseKotlin`, `:app:assembleOssRelease`, `test` pass.
  Final `:app:lintOssRelease` still fails on the existing 2370 errors/27 hints.
  XML multiset comparison by ID/severity/message/file gives 2397 baseline and
  final issues, zero additions/removals after normalizing the intentional
  SlipstreamInstance → MasterDnsInstance rename. No lint suppressions added.
- Additional `go test -race` passes flowbridge/client/arq/config. Upstream
  udpserver tests remain red: unsynchronized `testNetConn.closed` assertions in
  two cancellation tests reproduce on pristine `acbf1c6`; the intermittent
  cleanup TX-queue assertion also reproduces there under `-race -count=1000`
  (an initial 20-run check did not reproduce it). These are recorded upstream
  limitations, not green race-suite claims or migration regressions.
- Stable docs updated: `docs/dns-tunnel.md`, root `AGENTS.md`; outer operations
  `README.md`, `AGENTS.md`, `docs/n-de1.md`, `docs/access-paths.md`. Unrelated
  operations agent configuration left untouched. No commits/push performed.
- APK: `app/build/outputs/apk/oss/release/Owenclave-0.17.50-arm64-v8a.apk`, saved
  as ignored `build/masterdns/artifacts/clean.apk`; SHA-256
  `5fbee20c645ae73b81a75f4ffb6feb2ac556b6e244382a3f342faf05bc4a61f9`.
- Android native SHA-256:
  `ed4c3e93e9bbbaa06ec373900d290c165a628818c2e3bd267b19ff90988c7e3d`.
- Deployed server SHA-256:
  `8954b129d6007cc7114b6b79c7d52e6d028221b22e34990e0cd5be05b4ff82b4`.
- Deployed flowtls SHA-256:
  `248db446c2a3ac0080d618727226035ebdaf7d448ea5396d082aa75d122c9090`.

## Historical migration completion

R1–R3 verified. MasterDnsVPN migration is complete with explicit performance
regressions and existing lint/upstream race-test limitations. No new unresolved
blocker is attributed to this diff; no overall speedup or restricted-LTE acceptance
is claimed. Runtime credentials, private keys and traffic output are not retained
in tracked artifacts. Rollback requires both the old server and old APK.

## User-requested rollback to Slipstream — 2026-09-07

User requested restoring Slipstream now and deferring any MasterDNS-to-Slipstream
patch experiments. No performance patch was ported.

- Server: saved pre-switch units/enablement under
  `/etc/masterdns/backups/20260907T160453Z-rollback-slipstream`; verified units and
  nftables before `systemctl disable --now masterdns.service flowtls.service` and
  `systemctl enable --now slipstream.service`. Existing Slipstream binary/unit,
  certificate, 32-connection/30-second limits and unchanged flowd were reused.
- Final server checks: Slipstream active/enabled, NRestarts=0; flowd active/enabled,
  stable pre-existing NRestarts=1. MasterDNS/flowtls inactive/disabled. Public UDP
  :53 owned by Slipstream; private :40001 present, :40002 and :41924 absent.
  Outputs null, host-wide WARP masked, systemd/nft checks passed.
- Android: installed the exact accepted `build/dns-performance/h6.apk` with `adb
  install -r` successfully; profile data preserved. Restored SlipstreamInstance.kt,
  V2RayInstance.kt and AGENTS.md from `b53546d`. Removed MasterDNS from the active
  Kotlin and jniLibs directories, retaining copies in ignored
  `build/masterdns/rollback-source/`; retained `bin/lib/masterdns/` and this ledger
  for later analysis only.
- Rebuilt `:app:compileOssReleaseKotlin :app:assembleOssRelease test`: successful.
  APK SHA-256 exactly matches accepted H6:
  `965897285383ed942fc982a57943b2b90e559f49ac3aeaccb9fc6d4de3819047`.
  Slipstream native SHA-256:
  `785f4de12527f1c2a8311d1325a60ffca994d293d0dc5f146e2e994e07c18eb2`.
  Both active Kotlin sources are byte-identical to `b53546d`.
- Stable Android/operations documentation now describes Slipstream again, while
  retaining corrected automatic/token provisioning documentation and disabled
  MasterDNS experiment anchors. Unrelated agent configuration remains untouched.
- **Resolved external blocker for fresh E2E acceptance:** `measure.start()` returned
  `Start unavailable`; targeted `dumpsys window policy` confirmed `showing=true`,
  `mInputRestricted=true`. Wake/activity launch did not clear keyguard, and the same
  state persisted after the successful build. Child count is zero; VPN stopped.
  User was asked to unlock `ZY22JFJ5LP`; no security unlock bypass attempted.
  After unlock, run the existing exact download/upload and application UDP WARP
  checks against the already-installed H6 APK, then stop and record evidence.
  Prior acceptance of identical artifacts is not relabeled as a fresh rollback test.

### Fresh acceptance after user unlocked the device

- Same installed accepted H6 APK and manual TCP resolver; real captured Termux path.
  Exact 1048576-byte download: HTTP 200, 7.629958 s, **137.429 kB/s**.
  Exact 131072-byte upload followed by HTTP 200: 8.643258 s, **15.165 kB/s**.
  These are a single rollback smoke test, not a new A/B performance estimate.
- HTTPS egress DE/WARP. Application UDP STUN: valid 20-byte request/32-byte response,
  matching transaction ID and mapped-address hash matching the recent WARP namespace
  control. UDP exit differs from HTTPS exit; the separate UDP proof is retained.
- Exactly one carrier child while connected; successful normal stop leaves zero.
  Server after payload: Slipstream active/enabled, NRestarts=0; flowd active/enabled,
  unchanged NRestarts=1; MasterDNS and flowtls inactive/disabled. Only private
  :40001 remains, no :40002/:41924. Rollback is now complete.

### Deferred performance directions, not implemented or accepted

- MasterDNS's managed ARQ send-admission limit is not a QUIC congestion-window patch:
  48 ARQ packets, 48 adapter workers and QUIC bytes-in-flight are different quantities.
  Transfer the measurement question (is upload overfilling carrier queues?), not
  its literal constant or reliability implementation.
- First small diagnostic candidate: explicit Slipstream congestion-control A/B.
  Pinned `slipstream_mixed_cc.c` selects BBR for authoritative mode and DCUBIC for
  recursive mode; Android currently uses authoritative mode even through the local
  TCP DNS adapter. The existing `--congestion-control` override can compare the
  algorithms without changing resolver, transport, MTU or scheduler. This is not
  proof that BBR is wrong, nor that measured RTT is just loopback RTT.
- Next separate hypothesis: smooth the combined ordinary-query plus poll burst,
  preserving useful downstream supply and idle scheduling. Do not copy the rejected
  Busy poll-only cap or all-outstanding subtraction. MasterDNS's fixed 32-pending
  target/1-second expiry was not proven optimal for Slipstream.
- Do not transplant MTU 1000, Go ARQ or extra inner TLS into Slipstream. Query QNAME
  capacity and response TXT capacity differ; Slipstream already has authenticated
  QUIC streams. Rejected queue growth, adapter backpressure, TCP pipelining and
  same-resolver UDP experiments remain rejected without new evidence.
- Any later upload candidate needs fresh balanced paired confirmation and separate
  download/loaded-latency non-regression gates. No patches, commits or pushes were
  performed as part of this discussion.
