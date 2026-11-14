# Goal: measured Yandex DNS multipath

Status: complete
Source: explicit user instruction permitting iterative Yandex multipath experiments, including fixes after initial regressions.
Last updated: 2026-09-09

## Objective

Test one-child native multipath against the best measured Yandex TCP single path
and the accepted56/64 control. Retain a justified improvement or document an
evidence-based rejection/blocker and restore the exact accepted runtime.

## Frozen Contract

- R1: Verify official Yandex IPv4 labels, fresh phone control and server boundary.
  Primary evidence: official page, immutable hashes, direct controls, foreground
  single-resolver measurements and read-only service gates. Status: verified.
- R2: Implement bounded two-path candidate and test delays, reordering, duplicates,
  failure before/after readiness, exact bytes, half-close and cleanup locally.
   Primary evidence: focused harness using actual implementation. Status: verified.
- R3: Measure candidate on forced-DNS LTE; diagnose initial regression and test a
  meaningful narrow fix, rather than reject solely on first out-of-box loss.
  Adoption requires six NEW order-balanced pairs, median paired target gain >=10%,
  opposite loss <=10%, >=4 target wins and no failed required transfers. Report
  loaded p95/max and resource tradeoffs. Primary evidence: immutable per-window
   installed hashes and foreground bounded workload. Status: verified.
- R4: Preserve successes/failures, exact rollback, builds/checks and final installed
  identity; automatic/manual TCP/UDP WARP acceptance for retained runtime, unchanged
   server gates and zero children after stop. Status: verified.

## Constraints And Envelope

- User permits experimental deviation from single-resolver/no-multipath Android
  convention only. Keep authentication/stdin secrets, TLS pinning, gVisor,
  independent streams, fail-closed, bounded resources and privacy unchanged.
- Verified Yandex IPv4 TCP:53 only initially; two addresses do not imply independent
  capacity. Preserve --authoritative mode and explicit DCUBIC to isolate multipath.
- One child, separate stable loopback UDP endpoint per TCP path. Initial aggregate
  budget56 workers/64 queue: two28/32 versus each single56/64. No112-worker build.
- Target SlipstreamInstance.kt and focused tests; native patches/build script only
  for demonstrated startup/scheduling causes. Ignored build/dns-multipath helpers,
  immutable artifacts and aggregate-only measurements permitted. No server edits
  expected; no production traffic logs or payload captures.
- Preserve all existing worktree changes. No commits/pushes, unrelated edits or
  access to /home/stfu/Torrents/apk. One phone owner, no overlapping benchmarks.
- Asset/native equality checked before measurement; installed APK hashed each
  window. Never use :app:tasks --all (configuration-time asset mutation).
- Failed windows retained; only proven confounds invalidated explicitly. Rotating
  WARP fingerprint mismatch is inconclusive, not proof of leak or a silent pass.
- These are forced-DNS LTE tests unless direct-block preflight proves restriction.

## Execution Directive

Complete the frozen Required Outcomes using the listed Change Envelope and Primary
Evidence. Work on the smallest unresolved outcome. Do not add requirements from
reviews, tests, tools, speculative risks, or optional source text. Stop substantive
work at a proven external blocker, approved budget boundary, or when no remaining
in-scope action has a falsifiable expected result; record evidence and smallest unlock.

## Current State

- HEAD2658876; existing56-worker diff and rejected-deadline test fixture preserved.
- Control build/dns-iteration/workers56.apk SHA-256
  c156e88dba3cb462ba27a0908a17baf3b2e0f73a5c5af00e66f7b51724590623.
- Control native SHA-256785f4de12527f1c2a8311d1325a60ffca994d293d0dc5f146e2e994e07c18eb2.
- Retained opt-in APK: build/dns-multipath/fixed.apk,
  SHA-256 c8f2c1943dfd75dc40d35e5243942e5a302c5a6ae7907ce2447631a627ca36ac.
  Packaged native64cbc7d115c7b3b1f23be1687571c51ed7cde0403c5d9f4a150940028b9067e2.
- Moto g54 accessible through ADB. No general-agent tool available; phone stays
  under one foreground owner. Existing measure.py/accept.py inspected.
- Official https://dns.yandex.ru/ verified live2026-09-09: Basic77.88.8.8/.1,
  Safe77.88.8.88/.2, Family77.88.8.7/.3 (page lines20-33,87-90,177-180).
- Known baseline lint2371 errors/27 hints; no clean-lint claim or suppression.

## Current Checkpoint

Closed: R1-R4 verified. Retain the explicit experimental option, not an
automatic/default multipath change. No remaining execution checkpoint.

Confirmation target is acknowledged upload, following the selection screen;
download must not lose more than10%. Use six fresh alternating CT/TC pairs versus
.88 single on the same corrected server. Separately compare previous .8 single.

Minimal R2 envelope expansion: native runtime bootstrap may try each configured
endpoint once in the same child after a3s pre-readiness timeout. Two endpoints
remain below the owning15s VPN deadline (latency owner still cancels at5s).
No post-readiness stream migration policy, auth or pinning changes. Preserve a
reproducible patch alongside existing native patches before any retained artifact.

R2 reproduced native server SIGSEGV under local primary-loss stress. GDB without
frame arguments resolves stack to picoquic_create_packet_header -> protect/finalize
-> prepare_packet_ready -> prepare_packet_ex -> server.rs:388. The forced slot path
overrides the scheduler after abandoned-path deletion without validating index or
remote CID. Minimal envelope expansion: validate forced-path identity and readiness
across deletion in picoquic sender. The copied deployed binary reproduces SIGSEGV,
whereas the corrected Debian-compatible build passes all five local scenarios.
The user's autonomous optimization instruction therefore requires a narrow server
binary rollout before further multipath phone tests. Replace only
/usr/local/bin/slipstream-server after timestamped adjacent backup, dependency/CLI
and systemd validation; restart only slipstream.service. Preserve its exact args,
certificate, reset seed, limits, null output, flowd and WARP boundary. Roll back the
binary and restart that service on failed gates. No production fault injection.

## Checkpoint History

- Preflight: instructions, prior acceptance and rejected-deadline evidence read;
  official provider labels verified; no production edits yet.
- Fresh baseline: Wi-Fi0, LTE/LTE, accepted installed SHA matches. Direct ya.ru302
  in0.407391s, cloudflare.com301 in0.350952s: not Restricted LTE. Server
  Slipstream/flowd active/enabled, restarts0/1, output/error null; retired services
  inactive/disabled. Single .8 DL259734/UL18552B/s; one loaded timeout20.00308s,
  recovery passed, retained in single-8.jsonl. Singles .1/.88/.2 required gates pass.
- Host native build initially failed missing Perl FindBin with vendored OpenSSL;
  system OpenSSL alternative reached picoquic but lacked engine.h. Minimal R2
  envelope expansion: host-only picoquic include guard using existing
  OPENSSL_NO_ENGINE macro (all actual ENGINE calls already guarded). No algorithm,
  Android artifact or TLS verification change. Cache patch must be recorded and
  removed after host build; existing cache modifications are preserved.
- .7 UI input assertion failed before Save or child launch. Not a measured window;
  return to profile list and retry resolver entry only.
- All six single screens completed, B/s DL/UL: .8 259734/18552 (one loaded timeout),
  .1 253473/19622, .88 252055/24268, .2 252188/17581, .7 272644/11747,
  .3 257892/17326. Other required requests passed. Raw aggregate windows retained
  under ignored build/dns-multipath/single-*.jsonl. One window/address is selection
  evidence, not a statistically confirmed ranking. Freeze .88 primary/.1 secondary
  for best screened upload with stable second address; .7 is fastest DL but worst UL.
- Host native check using actual patched client/server and loopback fixture token/
  SOCKS auth: reorder/duplicate and fail-primary-after-ready each passed131072 exact
  bidirectional bytes plus half-close. Dead-primary-before-ready timed out20s.
  Output discarded; proxy pending responses bounded128, all children terminated.
  This tests native streams with a validating fixture backend, not deployed flowd.
- Balanced candidate: one child, two28-worker/32-queue adapters, .88-only experimental
  selection (not a shipped UI contract). Focused real adapter JVM test passed
  separate response source mapping and closing blocked receivers/connections;
  existing12 deadline fixture tests also pass. Release build/Kotlin succeeded.
  Incorrect release-unit-test task failed selection; debug unit task used instead.
- Candidate build/dns-multipath/balanced.apk SHA-256
  36b9363b61343b872da0036efa390f127fe1f5ceec92db05baff74bd54ca4022;
  SlipstreamInstance.kt a5309e7fccef6d4e32286d2ee52da90a824b38ba1ee40c50c8091dcfd33997d0.
  Pre-install zip gate initially stopped: only generated DEX baseline.prof/profm
  differ, as expected for changed code. Exclude exactly those two from equality;
  all other assets and native entries must match. No confounded phone window ran.
- Balanced screening CT then TC, .88 single versus .88/.1: C/T DL226210/255895,
  UL12278/22689 then DL199555/249815, UL16524/21470B/s. All required gates passed;
  all windows1 child then0; loaded maxima C0.659647/T0.696469s. Improvement warrants
  continuation, not adoption yet. These pairs are excluded from future confirmation.
- Bootstrap fix required two corrections: rotate resolver specs before path0 state
  creation, and destroy the old QUIC context before reset_for_reconnect because
  destruction callbacks set closing state. Dead-primary test now passes131072 exact
  bytes and half-close with0 replies from dead path. Some fail-after-ready local
  windows pass; others crash the server (-11), so success is not accepted as stable.
- Cached NDK path was absent. Official r29 zip fetched over HTTPS and SHA1 matched
  87e2bb7e9be5d6a1c6cdf5ec40dd4e0c6d07c30b from android/ndk r29 release; extracted
  to original cache path. Incremental arm64 native build succeeds without resetting
  cache source or rebuilding/replacing the accepted packaged libslipstream.so.
  Production Slipstream/flowd restarts remain0/1 after balanced phone screening.
- Deployed binary236b76c41211510ee94ee61616bb2ae8735c70bd6cf9df00d3c668c2837c6f93
  copied locally and exercised without production changes: fail-after-ready SIGSEGV
  reproduced. Fixed Debian12/OpenSSL3.0 build SHA-256
  d7667b1e6dde9ef4669cda08f576307d851b3ec8e32736ebdeabae4e9926191b passes reorder/
  duplicate, primary loss and dead-primary startup, each131072 exact bidirectional
  bytes plus EOF. Both-dead and wrong-pin exit1 with no readiness/payload. Wrong-pin
  initially retried without advancing; corrected pre-ready closure accounting.
- Native fmt passes. Strict clippy fails existing question_mark at server
  udp_fallback.rs:225 and collapsible_if in the older runtime diagnostics block;
  neither changed by this experiment. No suppression or clean-clippy claim.
- Server rollout gates passed (Debian dependencies/CLI, loopback certificate startup,
  systemd verify). Only Slipstream restarted. Current server d7667b1e6dde9ef4669cda08f576307d851b3ec8e32736ebdeabae4e9926191b;
  rollback /usr/local/bin/slipstream-server.bak-20260909T075648Z, original236b76c...
  Public53 and private40001 unchanged, restarts0/1, both output/error null.
- Fixed APK c8f2c1943dfd75dc40d35e5243942e5a302c5a6ae7907ce2447631a627ca36ac;
  native64cbc7d115c7b3b1f23be1687571c51ed7cde0403c5d9f4a150940028b9067e2.
  Bootstrap and picoquic patches retained with build integration; reverse checks pass.
  Release/Kotlin/debug JVM tests pass. Explicit tcp+mp://77.88.8.88:53 selects only
  Safe .88 / Basic .1; ordinary tcp:// remains single. Parser rejects other pairs
  and URI extras. Automatic startup/benchmark pools unchanged. Generated profiles
  and this exact native change are the only asset/native equality exclusions.
- First fixed confirmation series completed six CT/TC pairs. All candidate required
  requests passed, but control pair3 upload failed after75.001186s,131072 bytes
  submitted and no HTTP acknowledgement. Preserve it, not a valid upload ratio.
  Candidate pair3 was run after diagnosis, not silently paired across that pause.
  DE services stayed0/1 restarts; direct bound WARP same-endpoint DL1MiB and UL128KiB
  both returned200 in0.390/0.204s. This does not retrospectively locate the timeout.
  Five valid matched upload ratios cannot be called six-pair confirmation.
- Next falsifiable isolation: six NEW balanced pairs using the exact same fixed APK
  for both arms, single .88 versus explicit .88/.1. This separates the multipath
  effect from native lifecycle fixes; previous accepted comparisons and all failures
  remain evidence. No source/native/endpoint changes and no reset of prior results.
- Isolation completed without required failures in either arm. Both arms used fixed
  c8f2c194... APK, only strict .88 versus explicit .88/.1 differed. Same assets and
  installed identity gates passed each window. Table below includes the upload loss
  in pair3; no window discarded. Adoption thresholds met on these six fresh pairs.
- Final acceptance: manual multipath, manual single .88 and Automatic each passed
  exact1MiB HTTPS download, acknowledged128KiB upload and three authenticated-carrier
  UDP STUN20-byte requests/32-byte transaction-matching responses. Every UDP mapped-IP
  fingerprint matched three fresh CloudflareWARP-bound server references before and
  after the phone sequence (fixed public STUN destination162.159.207.0:3478). HTTPS
  trace DE/warp:on throughout. Each mode one child, stop zero. The old static
  fingerprint was not reused; this resolves current acceptance, not the historical
  mismatch or its unknown cause.
- Final five-case native harness passes with the exact deployed Debian server build:
  delayed/reordered/duplicated replies, primary loss after readiness and dead-primary
  startup each131072 exact bytes in each direction plus EOF. Both-dead/wrong-pin
  exit1 without readiness or payload. Fixture backend validates Flow OPEN and SOCKS
  credentials; it is not a replacement for production flowd/WARP acceptance.
- Cargo client/server test command succeeds:34 unit tests and integration test
  executables pass (some upstream integration cases may be environment-gated).
  Release/Kotlin/debug JVM tests pass; lint repeats2371 errors/27 hints, unchanged
  from the accepted baseline. Strict clippy still fails the two pre-existing lints
  above. No suppression. Temporary host-only engine.h include guard removed after
  host builds/tests; Android/server patches do not include it.
- Final server state: Slipstream/flowd active enabled, restarts0/1 unchanged during
  acceptance, stdout/stderr null; public195.128.101.186 UDP53 and private40001 only,
  retired MasterDNS/flowtls inactive disabled and private40002/41924 absent.

## Paired Results And Limits

Six same-APK isolation pairs, bytes/second, alternating CT/TC:

| Pair | Order | Single DL | Multipath DL | Single UL | Multipath UL |
|---|---|---:|---:|---:|---:|
| 1 | CT | 274473 | 274646 | 18360 | 24246 |
| 2 | TC | 208909 | 269289 | 16917 | 28555 |
| 3 | CT | 260841 | 248013 | 18216 | 10885 |
| 4 | TC | 245821 | 289944 | 24554 | 25348 |
| 5 | CT | 225788 | 262434 | 17638 | 22862 |
| 6 | TC | 223871 | 266964 | 16661 | 22798 |

- Median of within-pair ratios: DL+17.09%, acknowledged UL+30.84%;5/6 wins each.
  These are not ratios of unmatched medians. Median absolute rates C/T:
  DL235805/268126B/s, UL17927/23554B/s. Pair3 UL-40.24% remains a real loss.
- Loaded4KiB nearest-rank p95 C/T0.706636/0.695593s; max0.707677/0.726807s,
  30 probes per arm. No failed required warm/download/upload/idle/loaded/recovery.
- Four concurrent capped1MiB downloads: C8/24 and T24/24 completed within18s;
  aggregate received22538936/25165824 bytes. C partial bulk cancellations are retained
  expected capped-workload outcomes, not silently marked complete. This measures
  concurrent progress separately from one exact1MiB flow.
- Previous accepted .8 comparison screen: C/T DL208516/283004B/s,
  UL16492/25674B/s. Single pair only, not confirmation. Previous accepted .88 series
  has a required control upload timeout; it cannot provide six successful UL ratios.
- Mechanism evidence: equal aggregate56 workers/64 queue excludes simply doubling
  TCP workers as the explanation. QUIC now observes separate path RTT/loss and can
  use both resolver query-response opportunities; local per-path counters confirm
  both paths carry replies and surviving-path progress. Busy polling is per path,
  so equal worker counts do not establish equal DNS load, CPU or battery use. No
  live per-path throughput attribution or independent Yandex capacity was proven.
  Remaining variability and stream HOL are not claimed eliminated.
- Blocking adapter workers remain56 total, receivers increase1->2; child output
  drains/wait add3 (61 carrier blocking tasks versus default IO64, excluding other
  app work). Queue remains64 total and native process count1. No112-worker expansion,
  background ranking, periodic health probes, traffic logs or persistent counters.
- Meaningful fixes were driven by reproduced failures, not arbitrary scheduler
  sweeps: bounded bootstrap rotation with correct state reset and forced-path
  identity/CID validation. No extra ARQ or blind per-stream round robin was added.

## Reproduction And Rollback

Native source pins and build integration are in bin/lib/slipstream/build.sh;
new patches multipath-bootstrap.patch and picoquic-multipath.patch pass reverse
application checks on the built sources. Do not run the resetting build script on
another owner's modified source cache. The local Debian server was built in
owenclave-multipath-build:bookworm (rust1.97.1-bookworm plus cmake), source read-only,
using cargo build --locked --offline --release -p slipstream-server
--features picoquic-minimal-build, with separate PICOQUIC_BUILD_DIR/CARGO_TARGET_DIR.

```sh
env SLIPSTREAM_TEST_BIN="$PWD/build/dns-multipath/host-fixed/release" \
  SLIPSTREAM_TEST_CERTS="$HOME/.cache/owenclave-slipstream/src/fixtures/certs" \
  SERVER_BINARY="$PWD/build/dns-multipath/debian-server/target/release/slipstream-server" \
  python bin/lib/slipstream/test-multipath.py
python build/dns-multipath/summarize.py
env ANDROID_HOME=/home/stfu/Android/Sdk ./gradlew \
  :app:assembleOssRelease :app:compileOssReleaseKotlin :app:testOssDebugUnitTest
```

Phone scripts and immutable aggregate windows remain under ignored
build/dns-multipath/. Original single screening, balanced screening, fixed screening,
baseline, first confirmation (including timeout) and same-APK isolation are separate
files. Authentication material and payload contents are not retained in them.

For mode rollback select ordinary tcp://77.88.8.88:53 or Automatic: both were tested
with the corrected APK. Full APK rollback is build/dns-iteration/workers56.apk
c156e88d..., using manual single tcp://77.88.8.8:53, never tcp+mp. Its bundled native
is785f4de...; restore that exact library if restoring old source, not the new64cbc7d...
library. Preserve pre-existing56-worker changes and rejected deadline fixtures.
Server rollback anchor and compatibility warning are in operations docs/n-de1.md;
retaining the path-loss crash fix is intentional even for single-path clients.

Final source identities: SlipstreamInstance.kt
a36f426991e5c189cc753b6e891560e7a92e9a0e0312fde9a7d72776868c1ff1;
DnsttFmt.kt844e16904cad9c1cfc96536e3ba42264c1c6e3a9b47535b84a80ab764b8a761f.

## Completion

- R1-R4 verified by official resolver labels, immutable sequential/paired windows,
  implementation fault tests and three-mode phone TCP/UDP WARP acceptance above.
- Final phone installed c8f2c194... matches retained fixed.apk; explicit multipath
  selected, VPN stopped, zero Slipstream children. No production traffic logging,
  auth/pinning/gVisor/direct-fallback changes, commits, pushes or APK delivery.
- Final Gradle XML confirms16/16 JVM tests (4 multipath,12 existing deadline-fixture),
  no failures/skips. Native34 unit tests pass; existing integration executables pass.
  Release assembly/Kotlin pass. Known lint2371/27 and two unrelated strict-clippy
  failures are limitations, not green gates. Build script shell syntax passes.
- Both repository git diff --check commands and secret scans pass. Existing unrelated
  worktree changes preserved. Android instructions/runtime docs and affected DE/path
  operational docs reconciled; no topology or traffic ownership change.
- Retained native/runtime hashes and rollback anchors recorded above. Closure checked
  against the frozen contract; final status complete, with path-specific performance
  and battery/resource limits explicitly retained.
