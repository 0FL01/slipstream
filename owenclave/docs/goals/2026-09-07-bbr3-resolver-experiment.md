# BBR3 and resolver-aware pacing experiment

Status: complete — candidates evaluated/rejected; accepted DCUBIC restored
Source: user requests BBRv3 work and comparative tests; downtime permitted.

## Contract

Verify the real pinned BBR version, build a correctly labelled modern BBR3
backport, separately evaluate resolver-aware pacing, compare with accepted DCUBIC
and old BBR, and retain or reject using actual payload evidence. Preserve pinned
TLS/FlowRelay, one resolver/child, deadlines, gVisor, idle scheduler and WARP-only
egress. Do not deploy untested security/ABI changes or claim all network conditions
were tested. The supplied 100 MiB/multi-dimensional matrix is treated as a proposed
research method, not a license to flood public recursive resolvers; artificial
loss/rate conditions belong only on controlled local infrastructure.

Throughput remains the user priority. Candidate selection requires fresh balanced
comparisons, >=20% gain in a target direction without >10% opposite-direction
regression or failed payloads. Latency is measured/reported, not the superseded hard
10% gate. A rejected candidate is a valid experimental result; retain accepted
DCUBIC if no new candidate qualifies. No commits/push requested.

## Current checkpoint

Closed: six balanced four-variant blocks completed, plus a rejected model-only
400-QPS follow-up. Accepted DCUBIC source/native/APK restored and freshly verified.
Experimental sources and evidence remain under ignored `build/bbr3/`; no existing
cache reset, server changes or commits/push occurred.

## Findings

- Actual picoquic pin is `4bd356c004a50ec46ee8a933ebd024da5d659f75`, not the
  `d4a865c` cited in the supplied proposal. Its bbr.c explicitly describes BBR3,
  yet uses a 20% loss threshold; the researched upstream uses 2%.
- Current upstream pinned for this experiment:
  `0a1b5d35a562dd82ebe7061386392d4991dcf0c0` from
  https://github.com/private-octopus/picoquic . Updating bbr.c alone is insufficient:
  loss snapshots/ranges, ACK ECN path accounting and idle-restart producers also
  differ. Backport keeps the fork CC callback/descriptor ABI, rebuilds all consumers
  together, and avoids importing unrelated TLS/DNS changes.
- Web search authentication failed; direct upstream Git clone and official Android
  NDK download page were available. Installed isolated NDK r29 under ignored build;
  archive SHA1 matches official `87e2bb7e9be5d6a1c6cdf5ec40dd4e0c6d07c30b`.
- Native C objects for changed files and existing other CC algorithms compile with
  implicit-function and incompatible-pointer warnings treated as errors.
  Both new native candidates linked for Android and passed real LTE payload tests
  as detailed below; neither met adoption criteria.

## Implementation and build ledger

- C: modern BBR3 backport in isolated picoquic `bbr.c`, `picoquic.h`,
  `picoquic_internal.h`, `loss_recovery.c`, `frames.c`, `sender.c`. Supplies actual
  repeat-loss snapshots/ranges, path ACK ECN deltas and idle-restart events rather
  than zero-filled compatibility fields. Retains fork callback/descriptor interfaces,
  uses BBR-local helper compatibility, no wire/TLS/DNS changes. Struct sizes changed,
  so all native consumers were rebuilt. Upstream default options are used; legacy
  BBR tuning setters are not equivalent to the new options interface. This is an
  adapted backport, not a wholesale upgrade of the transport stack.
- D: C plus a separate Rust resolver controller. One authoritative resolver only,
  Busy only; ordinary queries and additional polls spend the same credits. Checks
  happen **before** QUIC packet preparation; nothing prepared is dropped just for
  lack of credits. Receive/command handling remains interruptible, no extra streams
  or traffic logs. Existing Warm/Quiet/Empty behavior and keepalives are unchanged.
- Model: initial 200 QPS, bounded 20–500, burst 8, continuous refill. DNS ID and
  canonical echoed question/type/class plus resolver peer must match. Pending state
  bounded to 4096; no outstanding ID overwrite. Observation windows 1–5 s based on
  smoothed DNS transaction time; at least 20 outcomes before adjustment. >2% failed
  or expired queries reduces rate by 20%; healthy saturated windows probe +5 QPS.
  Expiry uses RTT/variation with bounded 0.5–5 s backoff. This does not prove that
  a real resolver uses a policer, or that DNS transaction RTT is pure network RTT.
- Native build initially failed for missing Perl FindBin/File modules. Downloaded
  Fedora RPMs to ignored build directories and extracted locally. The first attempt
  mixed Perl 5.42.3 modules with system 5.42.2 and failed; using the matching extracted
  interpreter/library fixed it. No host package installation or test suppression.
- Both Android native builds and release APK builds passed. An initial harness call
  omitted its `run LABEL` arguments and failed before traffic; corrected invocation
  succeeded. The C screening window is excluded from paired estimates.
- Standalone model tests passed 8/8, including 48 deterministic local transaction
  simulations spanning caps 80/200/500, injected loss 0/2.5/10/100%, and service-time/
  jitter pairs 20/0, 100/80, 400/200, 1200/400 ms with impairment removal halfway.
  These are **model tests**, not a QUIC network-emulation matrix or 100 MiB transfers.
- Linked host `cargo test -p slipstream-client --locked --features
  openssl-vendored,picoquic-minimal-build`: 32 passed. Initial host configuration
  lacked zlib development linkage; locally extracted headers and explicit existing
  runtime library resolved it. No host-wide changes. The new model's Clippy warning
  was fixed; Clippy still reports the inherited `collapsible_if` in the unchanged
  battery-scheduler flow-block logging block. No warning suppression added.

## Fresh LTE comparison

Six blocks with order `ABCD`, `DCBA`, `BADC`, `CDAB`, `ACBD`, `DBCA`:

- A: accepted DCUBIC APK/native.
- B: accepted old `bbr` APK/native (already BBR3-like, not BBRv1).
- C: modern adapted BBR3, original polling.
- D: C plus resolver-aware pacing above.

Same manual TCP `77.88.8.8:53`, gVisor-captured Termux, LTE and DE/WARP checked each
window. Each window: 128 KiB warm-up, exact 1 MiB download, exact 128 KiB upload
followed by HTTP 200, 3 idle 4 KiB requests, 4 concurrent 1 MiB downloads with common
18 s cancellation deadline plus 5 loaded 4 KiB requests, recovery, one child and stop.
Upload endpoint returns empty body: not an independently echoed checksum. Partial
cancelled bulk bytes are not useful-goodput successes. No instrumentation overhead
was added to candidate APKs. Local controller tests are separate from this evidence.

| Block | A down/up kB/s | B down/up kB/s | C down/up kB/s | D down/up kB/s |
|---|---:|---:|---:|---:|
| 1 | 205.702 / 17.522 | 172.144 / 13.134 | 178.725 / 10.394 | 175.513 / 24.164 |
| 2 | 181.659 / 19.088 | 196.108 / 18.484 | 191.763 / 14.093 | 182.226 / 20.618 |
| 3 | 249.256 / 17.735 | 159.269 / 14.806 | 212.952 / 10.056 | 180.013 / 11.458 |
| 4 | 238.628 / 20.390 | 167.696 / 16.591 | 203.590 / 12.832 | 176.964 / 21.012 |
| 5 | 227.217 / 10.280 | 180.148 / 11.349 | 171.408 / 10.024 | 177.063 / 19.769 |
| 6 | 245.100 / 18.074 | 218.035 / 15.432 | 193.437 / 9.288 | 177.443 / **failed** |

| Aggregate | A | B | C | D |
|---|---:|---:|---:|---:|
| Median down kB/s | 232.923 | 176.146 | 192.600 | 177.253 |
| Median up kB/s, failed transfer scored zero | 17.904 | 15.119 | 10.225 | 20.193 |
| Idle nearest-rank p95, s (18 each) | 1.975268 | 1.821034 | 0.758254 | 3.905212 |
| Loaded nearest-rank p95, s (30 each) | 0.839068 | 4.915448 | 0.685984 | 0.724457 |
| Recovery p95, s (6 each) | 0.626605 | 3.855243 | 0.630338 | 0.897083 |
| Required non-bulk failures | 0 | 1 loaded | 0 | 1 upload |

Paired median relative changes (not ratios of aggregate medians):

| Comparison | Download | Upload |
|---|---:|---:|
| C vs A | -14.624% | -38.872% |
| D vs A | -23.957% | +5.533% |
| C vs B | +0.804% | -23.205% |
| D vs C | -6.621% | +55.020% |

The failed D upload is scored zero in paired upload estimates, not silently omitted.
It returned curl 35 / HTTP 0 / zero upload bytes after 11.578255 s. Subsequent payloads
passed. Harness stopped the series; the remaining B/C/A windows were then explicitly
completed without replacing D evidence. B also had a curl 35 loaded request after
10.360576 s. Cause of these TLS-level request failures is unproven; they are not
presented as proof of a specific congestion-control bug or a resolver rate limiter.
P95 above includes failure durations and must be read together with failure counts.

Decision: **reject C and D for production**. D improves a slow BBR upload baseline,
not the accepted DCUBIC trade-off. Neither gives the requested throughput improvement
over A without opposite-direction regression. Source-only BBR loss/ECN improvements
are not evidence of superior performance through this recursive DNS path.

## Follow-up: initial 400 QPS

Nearly flat D download around 175–182 kB/s suggested checking whether its initial
200-QPS budget restricted short transfers. Changed only initial QPS to 400 in the
isolated model. Two original unit expectations were tied to the old constant;
rewrote them as the same refill/multiplicative-decrease formulas, not relaxed gates.
Seven tests passed; the unchanged recovery test failed at cap=80, 10% loss,
1200 ms service time plus jitter: pre-removal minimum 67.108864 QPS, final 62.949673
after 30 seconds with loss/cap removed. This model still overreacts to transaction
timing/history in that case. **Rejected before building/installing an LTE APK**;
the failed recovery assertion was not suppressed. Retained rejected source and output,
restored 200-QPS model and reran the linked 32-test suite successfully.

## Scope limits

- No full 100 MiB × QPS × RTT × random-loss × jitter QUIC network matrix was run.
  No claim about all resolvers, random-loss superiority or real token-bucket behavior.
- No per-window DNS queries/MiB, actual QUIC retransmission/PTO counters or real DNS
  loss percentages were collected. HTTP completion/latency and model-internal
  synthetic loss are different evidence. No persistent traffic logging was enabled
  to fill those gaps. This is a bounded LTE candidate selection, not a complete
  characterization of congestion control under all listed impairments.
- Direct external LTE was reachable in the preceding baseline; no new claim of
  traversal of a currently restricted LTE allowlist. Server was not changed.

## Final state and evidence

- Restored Kotlin `--congestion-control dcubic`, accepted native and accepted APK.
  `compileOssReleaseKotlin`, `assembleOssRelease`, `test` passed; rebuilt APK hash
  exactly matches accepted DCUBIC:
  `df83feca21062cd637e5074aebd20afe386edbfd839510e3f1710538dba28e9e`.
  Native hash `785f4de12527f1c2a8311d1325a60ffca994d293d0dc5f146e2e994e07c18eb2`.
- Fresh restored-path check: exact 1 MiB down 4.529384 s; exact 128 KiB up 8.738951 s,
  HTTP 200; DE/WARP; valid application UDP STUN response matching the recorded
  WARP-namespace control hash. Normal stop left zero children; VPN left off.
- C APK `build/bbr3/modern.apk` SHA-256:
  `7e9c4ae9fe4e5c513f35ded2d0433f2ba35741411d9796dcda3188cb221e229b`.
- D APK `build/bbr3/paced.apk` SHA-256:
  `4860081ff12958646aff60c9791f567d460cae9a4a6398ef9a5ef102fd152ae7`.
- Safe JSON evidence: `build/bbr3/pair-{1..6}-{A,B,C,D}.txt`; model follow-up:
  `pacing400-results.txt`; host/build logs in the same ignored directory.
- Isolated full working source: `build/bbr3/src`; backport patch:
  `build/bbr3/modern-bbr3.patch`; Rust patch/new files and comparison/build helpers
  also preserved in `build/bbr3/source-evidence.tar.gz`, SHA-256
  `47109cd3d39af3e1454e04d51cb74f7e8db1f64aa2b576721ad2df93068075d7`.
  These are ignored experimental artifacts, not additions to the active build script.
- Stable docs link this result; pre-existing DCUBIC and MasterDNS history remains.
  No new active transport patch, server deployment, commit or push.
