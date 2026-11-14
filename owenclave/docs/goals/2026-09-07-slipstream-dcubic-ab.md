# Slipstream BBR / DCUBIC comparison

Status: complete (DCUBIC adopted by explicit user decision; latency trade-off retained)
Source: explicit user request to test the existing congestion-control override.

## Frozen contract

Compare accepted H6 default authoritative BBR with explicit `--congestion-control
dcubic`. Change no resolver, MTU, adapter workers/queue, native artifact, polling,
server or security boundary. Six fresh order-balanced pairs through production
gVisor on LTE. Accept only paired median upload gain >=20%, paired median download
regression <=10%, and pooled loaded p95 regression <=10%, with required payloads
complete and lifecycle/egress checks passing. Otherwise restore H6 and reject.

Control APK SHA-256:
`965897285383ed942fc982a57943b2b90e559f49ac3aeaccb9fc6d4de3819047`.
Native SHA-256:
`785f4de12527f1c2a8311d1325a60ffca994d293d0dc5f146e2e994e07c18eb2`.

Only intended code edit: two argv elements in SlipstreamInstance.startLocked.
No commits/push requested. Existing MasterDNS research/docs remain untouched.
Use the existing exact 1 MiB down, 128 KiB up + HTTP 200, idle/loaded 4 KiB,
18-second cancelled bulk and recovery harness. Upload HTTP completion is not an
independent echoed checksum. Partial bulk bytes do not count as successful goodput.
Direct external LTE was reachable earlier; do not claim restricted-LTE acceptance.

## Evidence

Phone unlocked at preflight. Build `:app:assembleOssRelease
:app:compileOssReleaseKotlin test` passed. Candidate APK SHA-256:
`df83feca21062cd637e5074aebd20afe386edbfd839510e3f1710538dba28e9e`.
Saved ignored `build/dns-performance/dcubic.apk`, measurements
`build/dns-performance/dcubic-{c,t}{1..6}.txt`. No instrumentation added.

## Six paired comparisons — 16:40–16:52 UTC

C = accepted default BBR, T = explicit DCUBIC. Same manual TCP resolver and
DE/WARP in every window; exactly one child during tests and zero after stop.
All required non-bulk requests passed in all twelve windows.

| Pair/order | C down kB/s | T down kB/s | C up kB/s | T up kB/s |
|---|---:|---:|---:|---:|
| 1 CT | 190.172 | 217.353 | 19.614 | 17.936 |
| 2 TC | 171.828 | 253.971 | 12.382 | 24.480 |
| 3 CT | 141.270 | 167.705 | 13.867 | 16.413 |
| 4 TC | 183.016 | 248.679 | 11.758 | 19.631 |
| 5 CT | 157.144 | 242.203 | 11.308 | 19.626 |
| 6 TC | 208.168 | 221.474 | 20.242 | 18.484 |

- Paired median relative change: download **+27.2954%**, upload **+42.6613%**.
  Download improves in six/six pairs; upload improves in four/six.
- Unpaired medians C/T: download 177.422/231.838 kB/s; upload 13.124/19.055 kB/s.
  Ratios of these medians are not the paired estimator.
- Loaded 4 KiB nearest-rank p95, 30 samples each: **0.907180/1.083902 s**,
  **+19.4804%**. Medians 0.653953/0.639654 s.
- Idle 4 KiB, 18 samples each: medians 0.587530/1.062895 s; p95
  2.050097/2.729378 s (+33.1341%). This additional observation is not a newly
  invented acceptance criterion; the original loaded-p95 criterion already fails.
- Recovery medians 0.513294/0.546974 s; p95 1.206204/1.140449 s.

## Decision

The decision below records the original acceptance gate. Subsequently the user
explicitly prioritized DL/UL over latency and instructed: "Вноси правки, берём
DCUBIC". This supersedes the default-selection decision, not the measurements or
the historical latency-gate failure. Reapply only the tested argv override, build,
install and verify TCP/UDP WARP payloads; no further performance tuning is in scope.

**Reject as default replacement:** throughput gates pass, loaded-p95 non-regression
gate fails (19.48% > 10%). Do not relax it or selectively exclude outliers.
This establishes a throughput/latency trade-off on this route, not statistical
certainty across networks or proof that BBR itself is defective. No internal
queue/cwnd attribution was measured. Retain the candidate artifact and evidence
for future, separately scoped work; do not add an unrelated scheduler patch now.

## Rollback

Removed the sole argv override. Last balanced window already reinstalled H6.
Final restored `assembleOssRelease compileOssReleaseKotlin test` passed; generated
APK SHA-256 exactly matches H6 above. SlipstreamInstance.kt matches `b53546d`
byte-for-byte. No runtime code changes remain from this experiment.

Post-rollback smoke test: DE/WARP, exact 128 KiB download in 2.298324 s and
128 KiB upload with HTTP 200 in 7.951994 s. These smaller smoke transfers are not
part of paired goodput estimation. Application UDP STUN passed (valid transaction,
20-byte request/32-byte response, mapped-address hash matches recent WARP control).
Normal stop leaves zero children; VPN left stopped. Server unchanged throughout:
Slipstream active/enabled NRestarts=0, flowd active/enabled stable NRestarts=1,
MasterDNS/flowtls inactive/disabled; output sinks remain null.

`git diff --check` passed. Both APKs and safe measurement outputs retained under
ignored build/dns-performance; no commits or pushes performed.

## Final adoption after user decision

Reapplied exactly the tested `--congestion-control dcubic` override, without other
runtime changes. Build `assembleOssRelease compileOssReleaseKotlin test` passed.
Generated APK SHA-256 matches the tested candidate above byte-for-byte; installed
successfully with `adb install -r`, preserving profile data.

Fresh adoption smoke test: DE/WARP, exact 1 MiB download in 5.510039 s and exact
128 KiB upload followed by HTTP 200 in 7.821407 s. Application UDP STUN passed
with matching transaction and recent WARP control hash. Normal stop leaves zero
children; VPN left stopped. These individual results are not a replacement A/B.

Current default is DCUBIC. BBR/H6 remains the rollback artifact. Original loaded-p95
gate failed; the user explicitly accepted that trade-off rather than the test
being reclassified as passing. Stable Android documentation updated. No server
changes, commits or pushes made for adoption.
