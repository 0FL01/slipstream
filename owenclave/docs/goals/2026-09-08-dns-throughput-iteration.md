# Goal: continue measured Slipstream throughput optimization

Status: complete
Source: user instruction, 2026-09-08, iterative RECON, hypotheses, practical phone tests, durable results, preserve security and rollback.
Last updated: 2026-09-08

## Objective

Evaluate evidence-selected changes against accepted DCUBIC on the available Android,
retain only a measured improvement, and leave an identified accepted runtime plus a
resumable next checkpoint. This bounded campaign does not claim globally optimal
DNS throughput or completion of an unbounded optimization objective.

## Frozen Contract

- R1: Obtain a fresh reproducible LTE control and safe rollback.
  Source: requested actual paired phone measurements and accepted rollback artifact.
  Acceptance: identified control APK/native, fixed resolver and existing foreground
  workload, direct baseline and server state. Primary evidence: hashes, ADB and
  aggregate-only results. Status: verified. Evidence: identified control and two
  fresh paired screening windows below, including a retained recovery failure.
- R2: Implement, screen and confirm evidence-led throughput candidates.
  Source: requested iterative implementation/testing; minor regressions acceptable.
  Acceptance: six fresh order-balanced pairs before adoption, paired median gain
  at least 10% in a target direction and no more than 10% opposite-direction loss,
  target gain in at least four pairs, no failed required transfers. Loaded p95 is
  reported, not a hard gate, consistent with the user's accepted DCUBIC trade-off.
  These are this campaign's predeclared selection thresholds, not a promised gain.
  Screening is separate and cannot substitute for confirmation. Failed required
  transfers remain recorded and score zero goodput. Primary evidence: existing
  measure.py workload with immutable C/T APKs. Status: verified. H1 meets gates;
  H2 screened against it and rejected, with samples retained below.
- R3: Record every substantial hypothesis, result, decision and rollback; verify
  the final accepted runtime and leave the next evidence-led checkpoint.
  Source: explicit durable iteration and invariant requirements.
  Acceptance: build/check results, exact TCP bytes/HTTP acknowledgment, application
  UDP, WARP egress, one child during traffic and zero after stop, automatic/manual
  setup if runtime changes are retained, stable server counters. Primary evidence:
  this document and existing accept.py probes. Status: verified. Evidence: final
  automatic/manual acceptance, artifact identity, server gates and closure below.

## Constraints And Envelope

- One child, one resolver, serial exchanges, bounded concurrency and separate bounded
  queue; independent QUIC streams, gVisor, credentials through stdin, pinned TLS,
  FlowRelay authentication, readiness/cancellation, unchanged WARP fail-closed.
- No security/resource-limit removal, direct fallback, pipelining, multipath,
  background benchmark, retained traffic logging, commits or pushes. Do not read
  `/home/stfu/Torrents/apk`. Preserve other worktree changes.
- Target: SlipstreamInstance.kt adapter constants initially; no native/server change.
  Existing measurement helpers may be wrapped under ignored build/dns-iteration/.
  Stable DNS docs change only for accepted behavior or an experiment link.
- Screening: two order-balanced pairs per distinct candidate; a promising candidate
  gets six new confirmation pairs. Keep the existing bounded per-window workload.
  Do not rerun rejected UDP transport, pipelining, Base36 or BBR3 without new evidence.

## Execution Directive

Complete the frozen Required Outcomes using the listed Change Envelope and Primary
Evidence. Work on the smallest unresolved outcome. Do not add requirements from
reviews, tests, tools, speculative risks, or optional source text. Stop substantive
work at a proven external blocker, an approved budget boundary, or when no remaining
in-scope action has a falsifiable expected result; record evidence and smallest unlock.

## Recon And Decisions

- Accepted source HEAD 2658876; existing uncommitted docs/dns-tunnel.md and
  docs/goals/2026-09-08-arq-recon.md preserved. Operations explore.md change untouched.
- Saved rollback build/dns-performance/dcubic.apk SHA-256
  `df83feca21062cd637e5074aebd20afe386edbfd839510e3f1710538dba28e9e`.
  Current release APK matches. Native SHA-256
  `785f4de12527f1c2a8311d1325a60ffca994d293d0dc5f146e2e994e07c18eb2`.
- Moto g54 reachable/unlocked by ADB, Wi-Fi off, LTE/LTE, operator labels t2/beeline
  (active data operator not established), VPN stopped and zero children at preflight.
  Direct ya.ru HTTP302 in 0.437s; cloudflare.com HTTP301 in 0.379s, with DNS/TCP/TLS
  completion. Restricted LTE not reproduced: these are forced-DNS LTE speed tests.
- Server read-only: Slipstream/flowd active/enabled, NRestarts 0/1, both output/error
  sinks null; MasterDNS/flowtls inactive/disabled. No outage injection authorized.
- General-agent delegation is unavailable in the exposed tool set. Independent
  source/device/server reads were parallelized; official Kotlin docs verified via
  codex_web. No delegated-agent results are claimed.

## Hypotheses

| ID | Falsifiable change and rationale | Risk / decision |
|---|---|---|
| H1 | 48 to 56 serial TCP workers, queue stays64. Earlier clean48 improved down; DCUBIC may still benefit from more concurrent resolver service. | More sockets/blocked threads can hurt upload or shared scheduling. 60 was considered but not built: receiver + two drains + child wait consume four other IO tasks, leaving no nominal room at default64. 56 leaves four; this is not a reservation guarantee. |
| H2 | Reduce queue64 to16: less queued obsolete QUIC work may improve useful goodput/loaded recovery. | More admission drops may instead lower throughput. Earlier suspending handoff removed drops without speed gain, so fewer drops are not the objective. After H1 adoption hold workers56, not48, to isolate queue size against the new best control. |
| Deferred | Strict exchange deadlines or preemptive-repeat policy. | Existing ARQ report has no LTE attribution; timer correctness needs focused tests and policy changes need native rebuild. Prefer constant-only reversible tests first. |

## Measurement

Use build/dns-performance/measure.py: production gVisor-captured Termux, strict
TCP77.88.8.8:53, Cloudflare endpoints; 128KiB warmup, exact1MiB download, exact128KiB
upload followed by HTTP200, three idle4KiB probes, four concurrent1MiB downloads
bounded to18s plus five loaded4KiB probes, recovery, child count and stop. Upload
acknowledgment is not an independently echoed checksum. Partial bounded bulk flows
are cancellations, not successful exact-transfer goodput. No telemetry added.
Compare median paired T/C ratios; pooled loaded nearest-rank p95 includes failures.

## Current Checkpoint

This two-candidate campaign is complete: H1 accepted; H2 rejected and queue64
restored. Final source/release/installed APK agree. H2 rollback is
build/dns-iteration/workers56.apk; earlier48 rollback remains dcubic.apk. Both retain
profile data. No phone experiment runs in the background. This closes a measured
iteration, not the broader optimization objective.

Next evidence-led iteration, not implemented here: reproduce adapter whole-exchange
age behavior with delayed connect/fragmented reads/local-send failure in an isolated
test, then test one strict-deadline/no-stale-retry candidate against accepted56/64.
Hypothesis: avoiding stale exchanges releases workers for useful QUIC recovery;
risk: premature expiry adds redundant loss/retries. Keep bounded concurrency,
cancellation and authentication unchanged; do not add production traffic logging.
The old8s socket timeout reused after connect is not a hard whole-query deadline,
as the prior ARQ report establishes. Smaller queue already lost; more shared IO
workers risk starving lifecycle tasks, so neither is the next blind sweep.

## Checkpoint History

- 2026-09-08: preflight and hypotheses above; no new APK/server change yet.
- H1 build: assembleOssRelease, compileOssReleaseKotlin and test pass with explicit
  ANDROID_HOME. APK build/dns-iteration/workers56.apk SHA-256
  `c156e88dba3cb462ba27a0908a17baf3b2e0f73a5c5af00e66f7b51724590623`.
  Existing UI helper's shift-selection appended rather than replaced resolver text;
  assertion failed before Save. Cleared only the known resolver field using bounded
  Delete key events, set exact TCP77.88.8.8:53 and saved. No credential fields read.
- H1 screen1 control: down207.791/up17.354kB/s, DE/WARP, one child, loaded probes
  0.557-0.635s. Recovery failed curl35/HTTP0 after15.484631s, zero bytes. Wrapper
  stopped and preserved the window, zero children. Server counters still0/1. This
  predates candidate installation and is not a candidate regression; exact cause
  unproven. Explicitly complete missing treatment half without overwriting control.
- H1 screen1 completed: T287.787/20.789kB/s versus C207.791/17.354; screen2 TC:
  T224.229/17.594 versus C214.373/17.848. Both candidate windows passed every required
  request, DE/WARP and child-stop gates; control2 passed. Screening paired median
  down about+21.5%, up+9.2%, but down effect differs substantially between pairs.
  Freeze APK above for six new confirmation pairs, excluding both screening pairs.
- H1 confirmation, six fresh CT/TC pairs, kB/s (decimal):

  | Pair/order | C down | T down | C up | T up |
  |---|---:|---:|---:|---:|
  | 1 CT | 218.511 | 257.345 | 16.764 | 18.817 |
  | 2 TC | 172.982 | 295.526 | 18.407 | 28.360 |
  | 3 CT | 174.837 | 229.380 | 17.790 | 18.650 |
  | 4 TC | 243.434 | 253.025 | 19.317 | 20.115 |
  | 5 CT | 234.763 | 200.616 | 13.936 | 18.877 |
  | 6 TC | 221.135 | 293.584 | 18.659 | 26.445 |

  Median paired ratios: down+24.4841% (5/6 wins), up+23.8501% (6/6 wins).
  Separate unpaired rate medians C/T: down219.823/255.185, up18.098/19.496kB/s;
  these are not the paired-ratio statistic. Loaded nearest-rank p95 (30 each)
  C0.819330/T0.847471s; maxima0.895684/6.187310s. All required requests pass,
  DE/WARP and one child throughout; zero after every stop. No failure removed.
- H1 acceptance: automatic and manual exact1MiB download plus acknowledged128KiB
  upload, valid20-byte STUN request/32-byte reply with matching transaction, one
  child and stop pass. UDP differs from HTTPS egress, as before; historical UDP
  hash had changed. Fresh STUN socket bound to CloudflareWARP inside server warpns
  yielded SHA-256 `445546cb7f1ea26865fa20728924a280892a7d0d608dcf6756ac7342db52bb72`
  using the existing test salt. Updated only that ignored harness expectation,
  then both phone modes matched it. No source routing/auth/pinning changes.
  Initial auto helper failed because install left activity closed; foregrounding
  existing activity fixed it. Fixed ignored accept.py resolver Delete selection;
  no profile credentials printed. H1 is the accepted current56/64 rollback APK.
- H1 checks: assemble/compile/test pass; lint with ANDROID_HOME fails with the same
  recorded2370 errors/27 hints as prior baseline, not a clean lint claim. A first
  lint invocation omitted ANDROID_HOME and yielded no usable diagnostic; corrected
  environment for the reported run. H2 now tests only queue16 at56 workers rather
  than discarding the proven H1 improvement. No native/server changes.
- H2 build/compile/test passed. Two CT/TC pairs at56 workers, Cqueue64/Tqueue16,
  kB/s: pair1 C263.891/19.624, T231.848/18.741; pair2 C301.212/20.765,
  T219.904/18.455. Both directions lose in both pairs (paired median approximately
  down-19.6%, up-7.8%). Loaded probes improved, but throughput is the objective;
  no six-pair confirmation justified. All required requests, DE/WARP and child/stop
  checks passed. Reject H2, retain its samples/APK under ignored build/dns-iteration,
  restore queue64 source. Final screen2 control already reinstalled accepted56/64.
- Final rebuild of56/64 passes assembleOssRelease, compileOssReleaseKotlin and test.
  Release and installed APK both exactly match workers56.apk SHA above; unchanged
  native matches preflight. Rejected queue16.apk SHA-256
  `3e8c465ce29aa7508e0bf38f24895919ee87f5cff76b0a7e3348b8f1f94f875e`.
  Phone remains manual TCP77.88.8.8:53, VPN stopped, Start visible, zero children,
  Wi-Fi0 and LTE/LTE. Server Slipstream/flowd still active, counters0/1, sinks null,
  public UDP195.128.101.186:53 and private warpns TCP10.200.0.2:40001 present.

## Completion

- R1-R3 verified for this bounded campaign. Accepted production source diff is only
  WORKERS48 to56; queue64, deadlines, retries, serial protocol and native unchanged.
  No remote files edited, services restarted, traffic logging enabled, outages
  injected, commits made or files staged. Pinning/authentication/fail-closed code
  is unchanged; no new induced-outage proof is claimed.
- Primary artifacts: ignored build/dns-iteration/compare.py, summarize.py,
  workers56-screen-{1,2}-{c,t}.txt, workers56-confirm-{1..6}-{c,t}.txt,
  queue16-screen-{1,2}-{c,t}.txt and the immutable APKs. Existing measure.py reused
  unchanged; accept.py only fixes resolver input and refreshes the observed UDP
  control hash. There are20 measured windows, including the failed control recovery.
- Source build/compile/test pass; no dedicated adapter unit tests were added and
  Gradle test must not be interpreted as such coverage. Lint remains red with the
  recorded pre-existing counts. Both repositories' git diff --check pass; final
  affected-file secret-pattern check emits no matches. Operations has only the
  unrelated pre-existing explore.md change. Prior ARQ docs changes preserved.
- Final status: complete for these two candidates. Broader throughput work remains
  open with the isolated deadline hypothesis above, no external blocker or unseen
  background work claimed.

## Sources

1. [Kotlin Dispatchers.IO](https://kotlinlang.org/api/kotlinx.coroutines/kotlinx-coroutines-core/kotlinx.coroutines/-dispatchers/-i-o.html), verified 2026-09-08: default parallelism max(64, cores), shared blocking IO pool.
2. [Prior clean-worker tests](2026-09-07-dns-tunnel-performance.md), [DCUBIC adoption](2026-09-07-slipstream-dcubic-ab.md), [ARQ findings](2026-09-08-arq-recon.md).
