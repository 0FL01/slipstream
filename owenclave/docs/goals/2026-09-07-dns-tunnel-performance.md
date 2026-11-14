# Goal: measured DNS Tunnel throughput improvement

Status: complete
Source: user request, 2026-09-07: implement hypotheses iteratively, build, compare before/after on the supplied LTE Android, document successes and failures.
Last updated: 2026-09-07

## Objective

Deliver a reproducible, measured DNS Tunnel performance improvement, retaining only
an accepted treatment and documenting failed experiments as well as successful ones.

## Frozen Contract

- R1: Establish a reproducible production-gVisor LTE baseline and a buildable control.
  - Source: requested before/after A/B tests.
  - Acceptance: exact download and acknowledged upload, short/loaded latency and
    recovery measurements with fixed resolver/endpoint and identified APK.
  - Primary evidence: bounded ADB/Termux workloads and aggregate-only diagnostics.
  - Status: verified
  - Evidence: built instrumented control `7ba99ec4...`; exact 1 MiB download,
    HTTP 200 after exact 128 KiB upload, loaded probes and recovery, one child and
    DE/WARP egress. The direct LTE control reached Cloudflare RU/warp=off, so these
    are forced-DNS LTE tests, not evidence of current carrier restrictions.
- R2: Implement and compare evidence-selected performance hypotheses.
  - Source: requested iterative code/build/A/B work and performance objective.
  - Acceptance: retain a treatment only with >=20% paired median target-direction
    goodput gain, <=10% opposite-direction and loaded-p95 regression, correct bytes,
    one child/resolver, unchanged readiness/cancellation and fail-closed boundary.
    Confirm using at least five order-balanced pairs, retaining failures in results.
    These are the proposed RECON decision gates, not a promised speedup.
  - Primary evidence: control/treatment builds and paired production VPN workloads.
   - Status: verified
   - Evidence: clean H6 six-pair confirmation below; download +34.939%, upload
     -6.583%, loaded p95 -27.913%, no failed required requests.
- R3: Document attempts and verify final runtime.
  - Source: explicit request to document successes/failures; repository verification.
  - Acceptance: hypothesis, exact change, checks, measured result and retain/reject
    decision for every substantive attempt; final non-instrumented APK accepted.
  - Primary evidence: this document, focused checks, final Android TCP/UDP payload,
    WARP egress, cancellation/stop and service restart counters.
   - Status: verified
   - Evidence: final H6 automatic/manual TCP and UDP acceptance, cancellation,
     restart recovery, unchanged server restart counters; all attempts below.

### Constraints

- One resolver and one child, gVisor only, independent application-flow QUIC streams.
- Preserve token/loopback authentication, certificate validation, WARP fail-closed,
  startup deadlines and existing Busy/Warm/quiet scheduling contract.
- No credentials, QNAMEs, user destinations or payloads in retained telemetry/output.
  Temporary instrumentation is aggregate-only and removed from final runtime.
- No multipath, resolver scanning, background ranking, direct fallback, security-limit
  removal, production exhaustion tests or unrelated service changes.
- LTE traffic is authorized; third-party resolver load remains bounded.

## Change Envelope

- Android `SlipstreamInstance.kt` TCP adapter and focused adapter tests; pinned
  Slipstream patch surfaces only if evidence requires a contract-compatible change.
- Temporary aggregate diagnostics and foreground ADB/Termux measurement harness;
  generated artifacts/harness under ignored `build/dns-performance/`.
- This goal and stable DNS docs only for a retained behavior change.
- No new runtime service, dependency, profile schema or transport protocol.
- Server is read-only initially. Any necessary controlled fixture expansion must be
  justified before editing and must not alter public ingress or WARP policy.

## Execution Directive

Complete the frozen outcomes using the smallest evidence-selected change. Continue
after failed candidates with a materially different hypothesis, not a renamed rejected
experiment. Preserve unrelated work. Stop only on verified acceptance or an observed
external dependency preventing the remaining work; never label failed tuning a win.

## Baseline and preflight

- Control: clean `11b35ff` before this document. JDK 21, Go 1.26.5, Rust 1.97.1,
  cargo-ndk 4.1.2 available. Existing generated native libraries and release APK exist.
- Android: Moto g54, Android 15, installed 0.17.50/1859; Wi-Fi off, data on,
  Termux run-as available. No profile credentials read.
- First build failed: SDK location absent from process configuration. Setting
  `ANDROID_HOME=/home/stfu/Android/Sdk` resolved it; no toolchain source workaround.
- Control APK SHA-256: `7ba99ec4defcd6ee3d66e24ac3f8d0fd2c3d9371539f4fa8747eb1c4457611e9`.
- Unchanged native SHA-256: `785f4de12527f1c2a8311d1325a60ffca994d293d0dc5f146e2e994e07c18eb2`,
  matching the previously accepted native in the all-outstanding goal.
- C0: download 131.262 kB/s, upload 16.066 kB/s; three idle probes
  1452/493/511 ms; five loaded probes 715/737/761/747/835 ms; recovery 510 ms.
  Four bulk requests were intentionally cancelled by their 18-second deadlines;
  their partial bytes are not counted as successful complete transfers.
- Adapter whole-window totals: received 8192, full-drop 1560 (19.04%), completed
  6632, errors/expired 0, queue HWM 66 (includes producer handoff races), cumulative
  dwell 143831 ms, cumulative service 866910 ms, lifetime 47967 ms. These are
  shutdown aggregates, not synchronized packet-level causality evidence.

## Measurement and material decisions

- Fixed manual resolver `tcp://77.88.8.8:53`, Cloudflare speed endpoints and native
  binary. Production gVisor VPN, not the foreground resolver-ranking UI.
- Each window: 128 KiB warmup, complete 1 MiB download, 128 KiB upload, three idle
  4 KiB probes, four concurrent 1 MiB downloads bounded to18 s with five loaded
  4 KiB probes, then recovery and stop. Incomplete bounded bulk flows are deliberate
  cancellations, not successful exact-payload measurements. Failed required requests
  remain in results and contribute zero goodput. Upload proof is exact curl upload
  size plus final HTTP200, not an independently echoed checksum.
- Gates use median of paired T/C ratios and nearest-rank pooled loaded p95 over30
  probes per variant. No parameter tuning during confirmation; screening is separate.
- H4 upload follow-up: five fixed 128/256 KiB uploads per variant all acknowledged;
  recovery passed. Transfers longer than15 s disproved a universal15-second endpoint
  deadline. The one H4 failure remains unexplained, not attributed to the provider.
- Removing counters changed measured effects at40 workers, including disappearance
  of upload regression. This new evidence justified testing clean48 (H6), which H3
  had not tested. Measurement interference and LTE/session variance are not separated;
  never pool clean and instrumented windows or keep telemetry to make a test pass.
- Clean control SHA-256 `dc163af932c18b95ad8676e1acaa815017499826dd304ce2a927fe45eb552cfd`;
  only source difference from HEAD was `WORKERS * 2` replaced by equivalent64.

## Experiment History

| Hypothesis/checkpoint | Change | Evidence | Decision |
|---|---|---|---|
| Build preflight | unchanged control | Gradle starts; fails because SDK location missing | configure existing SDK, not source workaround |
| Build retry | explicit existing ANDROID_HOME | release build passed; signed APK installs with data preserved | toolchain unblocked |
| C0 baseline | aggregate-only control | 19.04% adapter full-drop, exact payload and recovery succeed | test bounded handoff, not larger queue |
| H1 bounded handoff | `trySend` to suspending `send`; everything else unchanged | C0/T0/C1 download 131.262/129.849/141.059 kB/s, upload 16.066/15.225/16.050 kB/s; T0 local drops zero, control drops 19.04% then 2.86%; all exact/recovery probes passed | rejected at screening: removing local drops does not deliver the target goodput gain; original handoff restored |
| H2 bounded pipelining | 4 TCP lanes x8 outstanding; reassigned unique IDs, canonical QNAME/QTYPE/QCLASS match, generation close on error/timeout; original 64 queue | fake-resolver tests pass for reordered/fragmented answers, duplicate source IDs, compression, invalid IDs/questions/frames, timeout/close/reconnect; release built/installed; LTE T0 55.289 down/7.633 up kB/s, control C1 134.388/9.097; diagnostic repeat 74.631/9.801 | rejected; materially slower with increased loaded latency; remove candidate class and integration |
| H2 error attribution | aggregate category counters, no wire dumps | 226 reader I/O failures and 1274 failed exchanges; zero invalid-question/ID/QR/mismatch/timeout/full counters | not a measured demultiplexing error; transport-generation failures cause fan-out loss/retries. Exact peer/kernel cause not proven; do not claim all resolvers reject pipelining |
| Server resource check | read-only cgroup and service counters | Slipstream `nr_throttled=0`, `throttled_usec=0`; restart counters still 4/1 | no evidence to remove CPU/security safeguards |
| H3 worker capacity screening | 32 to48 workers, queue kept64; serial TCP and scheduler unchanged | C0/T0 download 138.459/192.981 kB/s (+39.38%), upload 14.005/13.245 (-5.42%); exact data, DE/WARP, one child and recovery pass | promising, not accepted; freeze six fresh confirmation pairs |
| H3 six-pair confirmation | unchanged treatment SHA-256 `3a7898dc5495b7c6829437fad77afa8539d7ad0f7375ae583ade1bd7ca380a61` | paired download +40.728%, upload -11.204%; pooled loaded p95 0.988591 to0.752361 s; no failed required payload/recovery/egress checks | rejected: upload exceeds the frozen 10% regression gate. Do not relax the gate for a near miss |
| H4 six-pair confirmation | 40 workers, queue64, SHA-256 `8535fbf00bd1d18d35e6f8627d5f91a923e76195314adb117b2008351d2a2e4c` | paired download +33.068%, upload -8.839% with failed upload scored zero; loaded p95 1.120074 to1.040294 s | numerical gates pass, but diagnose one unacknowledged upload before accepting |
| H4 clean confirmation | same40/64, all counters removed | six fresh pairs: down+2.666%, up+0.385%, loaded p95+42.793%; exact requests pass | rejected and reverted; supersedes favorable instrumented result |
| H5 receiver fairness | original32/64; cooperative yield after32 received datagrams, no timer or telemetry | T/C down139.439/160.533, up12.330/16.418 kB/s; no consistent loaded-latency win; payload/recovery pass | rejected at screening, yield removed; APK `ee712edc5fe9cb837d5b003a35cab4f75a939ff4e071ce0db6d84c80d736b22c` |
| H6 clean48 screening | 48 serial workers, independent queue64, no telemetry | T/C down191.341/139.944, up13.419/13.323 kB/s; required requests pass | freeze six new pairs2..7, excluding screening1 |
| H6 clean48 confirmation | unchanged clean APK | down+34.939%, up-6.583%, loaded p95-27.913%; zero required failures | retained after automatic/manual TCP/UDP and stop/restart acceptance |

### H4 confirmation

Same six-pair ordering and workload as H3; no screening samples pooled in.

| Pair | C down | T down | C up | T up |
|---|---:|---:|---:|---:|
| 1 | 116.976 | 160.780 | 12.768 | 11.293 |
| 2 | 90.591 | 165.624 | 14.370 | 10.679 |
| 3 | 138.894 | 162.028 | 14.782 | 15.102 |
| 4 | 125.406 | 172.088 | 11.871 | FAILED (0) |
| 5 | 136.940 | 153.384 | 12.170 | 11.424 |
| 6 | 134.669 | 173.605 | 14.755 | 13.953 |

T4 upload: curl rc52, HTTP000, size_upload131072, 15.440924 s: not acknowledged
and not successful. First subsequent 4 KiB probe7.575990 s; later probes/recovery
succeeded, one child remained. Full-window counters80674 received/68826 dropped,
11848 completed, no adapter I/O error/expiry. Attribution remains unproven.
All other required payloads, egress and stop checks passed. Download medians
130.038/163.826 kB/s, upload medians13.569/11.359 (including failure as zero).
Do not substitute ratio of those medians for frozen paired-ratio decision metric.

### H3 confirmation (screening excluded)

Rates are decimal kB/s; each pair uses TC for odd indexes and CT for even indexes.
Each window completes 1 MiB down, 128 KiB upload/HTTP200, three idle and five loaded
4 KiB probes, four bounded bulk flows, one recovery probe, then zero children after
stop. Both variants always report DE/WARP and one child before stop. Intentional
bulk deadline cancellations are retained but not scored as exact-payload success.

| Pair | C down | T down | C up | T up |
|---|---:|---:|---:|---:|
| 1 | 150.991 | 201.388 | 16.251 | 13.863 |
| 2 | 123.995 | 187.341 | 13.986 | 12.907 |
| 3 | 115.950 | 158.126 | 14.048 | 9.541 |
| 4 | 94.587 | 167.958 | 12.504 | 12.054 |
| 5 | 114.324 | 165.863 | 14.813 | 12.023 |
| 6 | 111.398 | 149.570 | 10.224 | 15.043 |

Download medians 115.137/166.910; upload medians 14.017/12.481. Acceptance uses
median of paired T/C ratios, not ratio of pooled medians. Loaded p95 uses nearest
rank over30 observations each. Phone USB powered, battery47–50%, temperature28–31C.
T adapter drops varied from3.46% to69.04%; zero-expiry throughout. The increased
download is real in these pairs, but queue-drop reduction is not its proven cause.

### H4 clean APK acceptance — failed

Treatment SHA-256 `9936de46c4da1cd2dbee23b894f9bf157ae2be39774ae3934e17a57c701fe61d`;
clean control `dc163af9...`. Both have no temporary counters. Same six-pair
TC/CT ordering, unchanged workload and resolver. All required exact payload,
HTTP acknowledgement, recovery, one-child and DE/WARP checks passed.

| Pair | C down | T down | C up | T up |
|---|---:|---:|---:|---:|
| 1 | 127.704 | 142.786 | 13.676 | 13.886 |
| 2 | 149.578 | 139.789 | 13.319 | 13.212 |
| 3 | 113.920 | 128.073 | 14.412 | 14.659 |
| 4 | 161.006 | 150.577 | 10.266 | 14.814 |
| 5 | 118.366 | 61.184 | 13.169 | 13.069 |
| 6 | 83.809 | 175.327 | 13.865 | 12.161 |

Paired download+2.666%, upload+0.385%; pooled loaded p95 C0.902176/T1.288230 s.
This supersedes the favorable instrumented H4 numerical result. H4 is not retained.
Release assembly and Gradle `test` pass. Lint fails with2370 errors, starting with
manifest MissingClass for AndroidX startup; identical issue multiset verified on HEAD.

### H6 clean APK confirmation — accepted

Six new pairs2..7, CT/TC/CT/TC/CT/TC, excluding screening1. Rates are decimal kB/s.

| Pair | C down | T down | C up | T up |
|---|---:|---:|---:|---:|
| 2 | 116.808 | 174.508 | 13.535 | 13.609 |
| 3 | 134.668 | 181.245 | 14.940 | 16.496 |
| 4 | 163.358 | 128.206 | 16.410 | 13.873 |
| 5 | 140.942 | 175.511 | 13.194 | 12.375 |
| 6 | 110.763 | 179.267 | 13.193 | 12.275 |
| 7 | 125.265 | 169.474 | 13.943 | 10.582 |

- Paired median download **+34.939%**, upload **-6.583%**. Pooled rate medians:
  down129.967/175.010, up13.739/12.992 kB/s; these are not the paired decision metric.
- Loaded p95 **1.247234 to0.899089 s (-27.913%)**,30 probes per variant.
- All required exact payloads/HTTP200, DE/WARP, recovery and child-stop checks pass.
  One download pair is negative; no cherry-picking. Phone temperature30–32C.

## Final runtime acceptance

- Installed the accepted non-instrumented H6 APK with existing profile data preserved.
- Automatic: exact1 MiB down5.101336 s; exact128 KiB upload/HTTP200 10.393896 s.
- Manual strict TCP: exact1 MiB down7.014996 s; upload/HTTP200 11.494621 s.
- Both modes: one child, HTTPS trace DE/WARP,20-byte UDP STUN request and32-byte
  valid response with matching transaction ID. The mapped UDP address matched two
  direct STUN controls bound to `CloudflareWARP` inside DE `warpns` (comparison by
  salted address digest, no address retained here). UDP and HTTPS public addresses
  differ; the initial cross-protocol equality assertion was invalid, not evidence
  of bypass. STUN is an application UDP probe through FlowRelay, not an ordinary DNS
  lookup or direct carrier upgrade. Endpoint source:
  [Cloudflare STUN service ports](https://developers.cloudflare.com/realtime/turn/).
- Screen-off30 s: exact4 KiB recovery486 ms. First stop attempt was blocked by the
  device lock screen; the4 MiB transfer completed, so that attempt is NOT cancellation
  evidence. After the user unlocked: stop during8 MiB download interrupted it after
  681792 bytes (curl56), zero children; restart4 KiB507 ms and DE/WARP, then zero
  children after final stop. Manual TCP override restored; VPN left stopped.
- DE Slipstream/flowd active; NRestarts remained4/1 from baseline to closure;
  output/error sinks null; host-wide WARP masked; systemd unit verification passes.
  No remote files, limits, routes or services changed. Fail-closed authentication,
  WARP binding, readiness and native activity scheduler remain byte-identical; no
  live carrier outage was injected and no new outage-resilience claim is made.

## Completion

- R1–R3 verified. Retained code: `WORKERS=48`, separate `QUERY_QUEUE_CAPACITY=64`.
  Original serial framing, handoff, deadlines/retry, cleanup and native binary stay
  unchanged. No temporary telemetry, pipeline class, yield or new dependencies remain.
- Final APK: `app/build/outputs/apk/oss/release/Owenclave-0.17.50-arm64-v8a.apk`.
  SHA-256 `965897285383ed942fc982a57943b2b90e559f49ac3aeaccb9fc6d4de3819047`.
- Commands: with `ANDROID_HOME=/home/stfu/Android/Sdk`, Gradle
  `:app:compileOssReleaseKotlin`, `:app:assembleOssRelease` and `test` pass. The test
  task reports NO-SOURCE for app unit tests; it is not new adapter unit coverage.
  Rejected H2 separately passed its fake-resolver Java tests before LTE rejection.
- `:app:lintOssRelease` is **not green**:2370 pre-existing errors/27 hints. Verified
  on byte-identical HEAD and final source: same2397 issue multiset (ID, severity,
  message and file), **zero new issues**. No suppression, baseline update or manifest
  edit. First closure command exceeded its200 s tool wait; longer retry completed
  with this same known lint failure. Relevant build/runtime gates pass independently.
- `git diff --check` passes. Stable documentation updated; no secrets or payload logs
  added. Ignored `build/dns-performance/` contains APKs, aggregate result files and
  foreground harness (`measure.py`, `pair.py`, `accept.py`); not part of a clean clone.
  No commits made. Rollback: restore the three-line worker/queue source diff and build,
  or reinstall saved `build/dns-performance/clean-control.apk` with `adb install -r`.
- Scope of result: this handset, LTE session, fixed TCP resolver and DE/WARP path.
  Direct external access worked in the control; this is not proof of Restricted LTE
  traversal, a universal speedup, or an energy improvement. Performance work ends at
  this verified decision gate, not at a claim that DNS capacity is unbounded.
