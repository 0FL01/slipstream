# Goal: test a strict DNS query deadline

Status: complete
Source: user instruction, 2026-09-08, implement and practically test the next DNS throughput hypothesis.
Last updated: 2026-09-08

## Frozen Contract

- R1: Reproduce stale serial TCP exchanges and prove deadline, cancellation,
  retry and fast-path behavior with isolated sockets using actual candidate code.
  Primary evidence: focused JVM tests and Android build. Status: verified.
- R2: Test immutable accepted56/64 control and deadline treatment on the phone.
  Two balanced CT/TC screening pairs first; reject if not promising. Adoption
  requires six NEW balanced confirmation pairs, paired-median target gain >=10%,
  opposite loss <=10%, >=4 target wins, no failed required transfers. Record loaded
  p95, allowing minor regression. Failed windows remain recorded, zero goodput.
  Primary evidence: existing foreground measure.py. Status: verified, rejected at screening.
- R3: Record candidate identity, result and exact restored/retained source and
  installed APK. If retained, automatic/manual TCP and UDP WARP acceptance; always
  zero children after stop and unchanged server invariants. Primary evidence:
   hashes, accept.py, read-only server state and this goal. Status: verified.

## Constraints And Envelope

- Keep workers56, independent queue64, serial persistent exchanges, DCUBIC and
  unchanged native; one child/resolver, gVisor, authentication/stdin credentials,
  TLS pinning, fail-closed and bounded lifecycle remain intact.
- No production traffic logging, remote edits, commits/pushes, unrelated worktree
  edits or reads of /home/stfu/Torrents/apk. No overlapping phone benchmarks.
- Target: SlipstreamInstance.kt TCP worker and a directly testable JVM exchange
  implementation; focused app/src/test tests and test-only JUnit dependency.
  Ignored build/dns-iteration helpers/artifacts may be extended. Stable docs only
  describe retained behavior or link the rejected experiment.
- Deadline implementation may use one dedicated scheduled executor per adapter,
  at most56 pending tasks with immediate canceled-task removal. No per-query
  thread, extra coroutine IO worker or unbounded timer backlog. Resolve the fixed
  resolver address once before receiving queries, not inside timed exchanges.
- Rejection restores original accepted56/64 runtime exactly; retain candidate as
  a test fixture and retain its measurements, rather than ship unused code.

## Execution Directive

Complete the frozen Required Outcomes using the listed Change Envelope and Primary
Evidence. Work on the smallest unresolved outcome. Do not add requirements from
reviews, tests, tools, speculative risks, or optional source text. Finish when every
required outcome is resolved and affected constraints remain satisfied.

## Preflight

- Prior completed campaign: [56-worker adoption](2026-09-08-dns-throughput-iteration.md).
- Source HEAD2658876 with existing workers48->56 diff, DNS docs and two goals;
  operations explore.md untouched. General-agent tools are unavailable.
- Control build/dns-iteration/workers56.apk and installed APK SHA-256:
  `c156e88dba3cb462ba27a0908a17baf3b2e0f73a5c5af00e66f7b51724590623`.
  Earlier rollback build/dns-performance/dcubic.apk:
  `df83feca21062cd637e5074aebd20afe386edbfd839510e3f1710538dba28e9e`.
  Native: `785f4de12527f1c2a8311d1325a60ffca994d293d0dc5f146e2e994e07c18eb2`.
- Moto g54: Wi-Fi0, LTE/LTE, labels t2/beeline (active operator unproven), VPN
  stopped, zero children. Direct ya.ru HTTP302, DNS/TCP/TLS/total
  .262477/.309016/.372370/.428221s; cloudflare.com HTTP301,
  .065948/.162642/.260276/.346393s. Restricted LTE not reproduced.
- Read-only server: Slipstream/flowd active/enabled, NRestarts0/1, stdout/stderr
  null; MasterDNS/flowtls inactive/disabled. No outage injection.
- Prior lint baseline2370 errors/27 hints remains red; do not suppress it.

## Hypothesis And Current Checkpoint

H3: A hard10-second age from enqueue closes an in-flight resolver socket even
during connect, blocked write or fragmented reads; retry shares the same budget.
The existing8-second per-socket timeout is retained inside that bound. Expired
responses are dropped and downstream UDP send failure cannot replay upstream work.
Fewer stale workers may improve useful throughput; extra scheduling/expiry loss
may instead hurt it. No performance benefit is assumed from synthetic correctness.

H3 rejected; accepted56/64 source, rebuilt APK and installed APK identities match.
No confirmation/adoption. Closure complete; no further tuning.

## Evidence And Decisions

- First focused invocation used unavailable :app:testOssReleaseUnitTest and failed
  task selection before compilation. :app:tasks --all established the supported
  :app:testOssDebugUnitTest. Corrected invocation plus assembleOssRelease and
  compileOssReleaseKotlin passed; initial10 tests pass, no test failure hidden.
- Actual exchange class is imported by the tests, not extracted from source text.
  Original blocking-read specimen delivered a fragmented reply after about637ms
  despite a300ms query-age budget. Candidate expires the same scenario at300ms
  without delivery. Fast reuse100 queries, downstream failure/no replay, expired
  queue, bounded retry, shared retry age, invalid length, deadline/cancel during
  connect/write/read and56 simultaneous stalled workers pass.
- Android Socket.close releases blocked socket IO [1]; removeOnCancelPolicy removes
  canceled timers immediately [2]. One independent scheduler remains available
  while56 IO workers block. No timer is scheduled for an already stale queue item.
- Review caught synchronous hostname resolution in the initial adapter constructor;
  before any phone build was installed it was moved into the existing receiver IO
  coroutine before query admission. Launch/close do not wait for it; a late result
  cannot start workers after close. The platform hostname lookup itself is not
  interruptible by Socket.close, as before, but now at most one lookup per adapter
  rather than one per worker. Numeric test resolver has no such lookup delay.
  This is bootstrap behavior, not a claim of hard real-time DNS lookup cancellation.
- Added explicit delayed-connect/read-budget and final-age-before-timer tests before
  freezing the candidate. Production socket timeout remains8s, max queue-inclusive
  age10s. Timer completion lock prevents an already-running canceled task closing
  a subsequent query's socket. Local reply callback is outside resolver retries.
- Final12 focused tests and Gradle test pass (7.929s focused suite); assemble and
  compile passed before the final two test-only additions. Immutable treatment:
  build/dns-iteration/deadline.apk SHA-256
  `ff7a59cfe793cfdee8ced3567f3f9da0f29ada78c6a8e6f803e4632357974e6c`.
  Exchange source SHA-256
  `f82df10d6dff2ca1b20211c890a7bbf2d23d02115b4420fc040cd13beeaf5167`;
  candidate SlipstreamInstance.kt
  `357f6f6e9e74d639436c30ff7c9a9d2ea90c9da3e1bb0dfb09cb73f71be649aa`.
  compare.py checks immutable file AND installed hashes before every deadline window.
  Foreground helper configured exact manual TCP77.88.8.8:53 with zero children.
- Initial screening CT/TC (decimal kB/s): C208.267/17.068,T258.621/19.757;
  T288.540/19.067,C248.932/16.633. All requests/egress/child gates pass, but these
  are INVALID for selection: zip-entry comparison found different GeoIP/geosite
  data and version assets between C/T. The task-discovery :app:tasks --all command
  realized setupApp's updateAssets registration body at configuration time, which
  downloaded new assets (deadline-tasks.txt lines13-21). These are our generated
  changes, not unrelated user work; restored all four assets directly from the
  accepted APK using unzip. Preserve ff7a... APK and deadline-screen-* samples;
  rebuild as a distinct deadline-clean identity and repeat both screening pairs.
  No source/runtime policy change or throughput credit from these invalid windows.
- Candidate lint fails2371 errors/27 hints (deadline-lint.txt), versus prior
  recorded2370/27. Only Slipstream diagnostic is MemberExtensionConflict at the
  unchanged ready.cancel() expression; none in DnsTcpExchange. No suppression or
  clean-lint claim. Asset restoration and fresh final lint must be recorded.
- Rebuild passed (deadline-clean-build.txt). Immutable deadline-clean.apk SHA-256
  `38c75f59cc660535d80a278123db2d770d02ebcba35d36578af7126e4f02c0fc`.
  All core assets and native libraries match workers56.apk byte-for-byte; changed
  ZIP entries are only classes.dex/classes2.dex, their dexopt profiles and signing
  manifests. Source hashes unchanged. Version-file worktree drift removed.
- Clean screening rejected H3: paired median DL-4.0330058%, UL+4.9036924%, one win
  each in two pairs, neither meets the10% promising threshold. No six-pair
  confirmation is justified. Aggregate median DL C258947.082,T250592.983B/s;
  UL C20045.231,T19585.239B/s (median of paired ratios is the decision metric).
  Loaded nearest-rank p95 C0.637030,T0.738714s (+15.96%),10 probes each, same maxima;
  zero required failures, all four windows DE/warp:on, one child, zero after stop.
  All optional bulk cancellations remain in raw output, never counted as successes.
- Valid CT pair1: DL C275910.267,T298764.966B/s; UL C26058.047,T21082.475B/s.
  Valid TC pair2: DL T202421.000,C241983.898B/s; UL T18088.003,C14032.415B/s.
  Files build/dns-iteration/deadline-clean-screen-{1,2}-{c,t}.txt; summary command
  `python build/dns-iteration/summarize.py deadline-clean screen 2`.
- Rejection restores only this iteration's runtime edits. DnsTcpExchange.kt moves
  byte-for-byte to app/src/test/java/io/nekohasekai/sagernet/bg/proto/, keeping the
  exact tested candidate outside the APK. Its12 tests and JUnit test dependency
  remain. Candidate adapter snapshot build/dns-iteration/deadline-SlipstreamInstance.kt
  and both immutable candidate APKs retain reproducibility. The preexisting56-worker
  change, docs and goals remain untouched except the new experiment link.
- Restored SlipstreamInstance.kt equals HEAD with only WORKERS48->56, SHA-256
  `6e8c04e0aee46e721c74aef092a9b9201361b3b0b6f04a1b9f4b302c38888eb3`.
  Rebuilt release APK equals accepted workers56.apk byte-for-byte (c156e88d...).
  Focused12 tests, Gradle test, assemble and compile pass after fixture relocation.
  Candidate fixture SHA remains f82df10d..., with no production reference.
- Extra restored automatic acceptance: exact1MiB TCP down and128KiB up pass,
  DE/warp:on; UDP20-byte request/32-byte valid matching-transaction reply arrived,
  but helper rejected the old fixed warpns fingerprint. Retained failed check at
  deadline-restored-automatic.txt; do not claim it passed. Four fresh server STUN
  probes with SO_BINDTODEVICE=CloudflareWARP in warpns all returned new salted
  fingerprint `a377e8acc0b624d72840b1e49c9a2a07caac468be8b081b190780161b0e5578b`.
  Updated only ignored accept.py's stale expected fingerprint; repeat automatic
  and manual checks against this live reference without printing public egress IPs.
- Automatic live-reference recheck passes: exact1MiB down/128KiB up HTTP200,
  matching20/32-byte UDP STUN transaction and live warpns fingerprint; HTTPS
  DE/warp:on, one child. UDP and HTTPS NAT addresses differ, so equality of those
  two addresses is not used as the WARP proof. File deadline-restored-automatic-live.txt.
- Extra manual restoration check has a retained failure:1MiB down passed,128KiB
  upload wrote all bytes but timed out waiting for HTTP at75.001642s (curl28), so
  required upload result is FAILED, not credited. File deadline-restored-manual.txt.
  Direct stopped-VPN upload control passed HTTP200 in0.955297s. One bounded manual
  recheck passed TCP down in3.548923s and up in4.619432s, DE/warp:on, and received a
  valid20/32-byte UDP transaction, but its UDP fingerprint no longer matched the
  fresh reference. File deadline-restored-manual-recheck.txt remains FAILED for
  UDP egress acceptance; cause of mismatch is not established, do not call the
  entire manual acceptance green or erase the upload failure. No further retries
  or server changes. These are additional RESTORED CONTROL observations, outside
  the four valid C/T screening windows; they do not alter H3 rejection.
- Final restored lint remains2371 errors/27 hints, identical to candidate lint,
  not the historical2370/27 count. Release APK and production source exactly match
  accepted control despite lint remaining red. No suppression added.
- Server counters unchanged Slipstream0/flowd1, both active/enabled with null
  output/error; MasterDNS/flowtls inactive/disabled. Public UDP195.128.101.186:53
  and private10.200.0.2:40001 present; private40002/41924 absent. No remote edits,
  traffic logging, credentials output or outage injection occurred.

## Completion

- R1:12 focused tests pass before and after test-only relocation (final7.996s);
  Gradle test, assembleOssRelease and compileOssReleaseKotlin pass. Initial wrong
  task invocation and red lint are recorded above, not hidden.
- R2: rejected at two valid balanced screening pairs; neither direction meets10%.
  Six confirmation pairs and candidate automatic/manual adoption are not applicable
  because no candidate is retained. Invalid asset-confounded windows preserved.
- R3: exact accepted56/64 source and rebuilt control APK; installed SHA c156e88d...
  verified, manual TCP77.88.8.8:53 restored, VPN stopped with zero carrier children.
  Native/authentication/TLS pinning/fail-closed/gVisor boundaries unchanged.
- Reproduce local checks with ANDROID_HOME=/home/stfu/Android/Sdk:
  `./gradlew :app:testOssDebugUnitTest --tests io.nekohasekai.sagernet.bg.proto.DnsTcpExchangeTest test :app:assembleOssRelease :app:compileOssReleaseKotlin`.
  Do not use :app:tasks --all here: its configuration realizes asset update bodies.
- Valid screening commands: `python build/dns-iteration/compare.py deadline-clean screen 1`
  and `python build/dns-iteration/compare.py deadline-clean screen 2` (immutable
  output paths deliberately reject rerunning over existing evidence).
- Retained changes: exact rejected exchange under app/src/test, its12 tests,
  test-only JUnit dependency, this goal and stable DNS-doc experiment link.
  Preexisting worker56 diff, docs/goals and unrelated operations work preserved.
- Closure: git diff --check and scoped secret-pattern scan pass; final installed
  hash and zero children reverified after the last manual recheck. No production
  deadline code remains. Source/build equality is exact, not inferred from version
  labels. Test report preserved as build/dns-iteration/deadline-tests.xml.
- Final status: complete, hypothesis rejected. Forced-DNS LTE evidence only;
  Restricted LTE was not reproduced. Additional restored-manual UDP fingerprint
  acceptance remains inconclusive and its failed windows remain explicit above.

## Sources

1. [Android Socket.close](https://developer.android.com/reference/java/net/Socket),
   verified2026-09-08, blocked IO throws SocketException on close.
2. [Android ScheduledThreadPoolExecutor](https://developer.android.com/reference/java/util/concurrent/ScheduledThreadPoolExecutor),
   verified2026-09-08, removeOnCancelPolicy supported since API21.
