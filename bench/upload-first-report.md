# Upload-first Python evidence — corrected 2026-09-27

## Decision and supersession

**The NOERROR candidate does not meet the ≥20% loaded-upload screen.** In the
corrected 100 ms TCP-DNS model, loaded 512 KiB uploads took **21.2118 s summed
for A versus 22.1303 s for B**: B was **4.33% slower**, with one paired win and
one loss. All corrected uploads completed with authenticated SHA256
acknowledgements. Six confirmation pairs were not triggered. This evidence
does not justify an APK rebuild.

**All earlier delayed-model results are invalid and superseded.** The original
shared UDP Observer had only one `self.peer` for all TCP workers: replies could
go to the last worker rather than their owner. Its timeouts, stalls, ID errors,
throughput and client/server-bottleneck interpretations must not be used.
The old 4-second deadlines and listener-only readiness also failed to establish
meaningful upload completion evidence. Earlier direct single-peer measurements
remain historical observations; they are summarized separately below.

## Exact artifacts

| Role | Local binary | SHA256 |
|---|---|---|
| A, preserved baseline C server | `build-upload-baseline/slipstream-server` | `b7c6b7ce4905adde4108b820115f1fce538d4afa79782fba6745203e5d2b3702` |
| B, isolated NOERROR C server | `build-candidate/slipstream-server` | `b9aedcd05127903f896a1716f45a621985d189e660f24220d0e6a48e8a832c75` |
| Unchanged Rust Linux client | `../slipstream-rust-control-artifacts/slipstream-client` | `af32d5e5418a7ab2a3c807bf10c6229f13afe709a1ab07ec8c915768e95461f7` |

Only Python probe, Python regression, and this report were changed by this
workstream. Native/source tests belong to the separate source workstream.
No remote node, production resolver, Android source, or real traffic was used.

## Corrected method and regression

- `bench/upload_first.py` reuses `bench.run.socks_open`,
  `bench.adapter.Adapter`, and `bench.backend.close_writer`. Authenticated
  fixed Flow OPEN framing follows `tests/test_runtime.py` and the benchmark
  backend. The fixture accepts only the synthetic target `127.0.0.1:40100`;
  it never forwards arbitrary destinations.
- Synthetic Flow token and RFC1929 credentials are generated in memory and
  provided through stdin. Ephemeral certificate/key files live in ignored
  `.cache/` temporary directories. Native stdout/stderr are `/dev/null` and
  traffic-debug environment variables are removed. No QNAME/payload/key logs.
- Every fresh native arm must complete a **1 KiB digest-acknowledged warmup**
  before timing samples. All eight corrected arm warmups completed in
  **0.470–0.480 s**, independently proving transport readiness. Listener
  readiness alone is insufficient. Warmups are excluded from sample timing.
- The corrected synthetic delay model uses the existing 56-worker, 64-entry
  drop-new queue and persistent framed TCP DNS connections. Each TCP worker
  owns an independent connected UDP socket **directly to the C server**;
  there is no shared Observer forwarding hop. Its DNS ID and QR are checked
  before sending the reply to that worker's TCP connection. IDs are unchanged.
  Aggregate reply counters are shared, but routing state is not.
- The retained direct UDP Observer now uses a bounded 4,096-entry DNS-ID to
  peer/age mapping. Conflicting peers with the same outstanding ID and capacity
  overflow are counted and dropped rather than misrouted. Only ID, endpoint,
  and expiry are retained in memory; no QNAME is stored or logged.
- `bench/test_upload_first.py` has **two passing concurrent-peer regressions**:
  two peers send distinct IDs, the synthetic origin sends responses in reverse
  request order, and both the Observer and actual TCPModel return the original
  ID to its correct owner. The TCP test also proves two distinct origin UDP
  sockets and zero invalid replies.
- Delay is 50 ms before and 50 ms after each exchange. This is **synthetic**:
  no recursive DNS, cache, LTE/radio, actual resolver policy, or carrier allowlist.
- Primary screen: **512 KiB standalone and alongside continuous verified
  download**, with a 60-second deadline. Two fresh baseline repetitions form
  the initial A/A; then fresh AB and BA arms supply two paired screens.
- Sustained check: only after 512 KiB completed, one fresh arm per binary sent
  **4 MiB standalone and loaded**, with a 180-second deadline. This is a single
  AB sustained check, not a six-pair confirmation experiment.
- Timing includes opening the authenticated SOCKS stream and the returning
  `OK + SHA256(payload)` acknowledgement. All payloads are fixed synthetic `x`
  bytes. Download counting now covers exactly the timed upload interval,
  excluding first-byte warmup and cleanup. Fixture streams are stopped between
  samples. Arms retain the same native tunnel across their two measurements.

## Corrected measurements

All **16 measured uploads** completed with exactly 524,288 or 4,194,304
digest-acknowledged bytes; zero deadline failures, fixture validation errors,
download errors, or dead native processes. Eight acknowledged warmups are
additional readiness evidence, not included in the 16 upload samples.

### Primary 512 KiB

| Run | Standalone seconds | Loaded seconds | Loaded download bytes |
|---|---:|---:|---:|
| Baseline A/A first A | 9.0912 | 11.0172 | 4,785,139 |
| Baseline A/A second A | 8.9375 | 9.8122 | 4,533,128 |
| AB: A | 9.2005 | 10.2640 | 4,723,870 |
| AB: B | 11.0377 | 10.1401 | 4,684,675 |
| BA: B | 9.0428 | 11.9902 | 4,938,495 |
| BA: A | 9.2034 | 10.9478 | 4,842,686 |

Loaded AB gain was about **1.21%**; loaded BA was about **9.52% slower**.
Summed standalone A 18.4038 s / B 20.0805 s: B **9.11% slower**.
Summed loaded A 21.2118 s / B 22.1303 s: B **4.33% slower**.
Acknowledged loaded aggregate goodput was approximately **48.3 KiB/s A /
46.3 KiB/s B**. Loaded p95 across the two screen samples equals their maximum:
**10.9478 s A / 11.9902 s B**; two observations do not establish a robust tail.

Screen download rate was approximately **451 kB/s A / 435 kB/s B**, about 3.6%
lower for B. This small paired screen does not establish a formal download-loss
or persistent-idle acceptance result. Baseline loaded variation itself was
11.0172 versus 9.8122 seconds.

### Sustained 4 MiB, one AB check

| Binary | Standalone seconds | Loaded seconds | Loaded download bytes | Loaded acknowledged goodput |
|---|---:|---:|---:|---:|
| A | 70.6822 | 81.0925 | 36,886,559 | ~50.5 KiB/s |
| B | 70.8132 | 77.3193 | 35,526,370 | ~53.0 KiB/s |

Standalone was effectively equal; B loaded time was **4.65% shorter** in this
single check, still below 20% and not confirmed. Both completed within the
180-second bound. Loaded download rates were approximately 455 kB/s A and
459 kB/s B. Do not combine this unreplicated result with the primary screen
to claim a reliable speed gain.

## Corrected carrier counters and stalls

Counts cover the measured upload through cleanup; in-flight replies can cross
sample boundaries. Empty NOERROR means ANCOUNT=0; QUIC contents are not inspected.
Adapter counters are cumulative per fresh arm, including warmup and standalone
work when observing loaded rows.

| Group | Queries | Replies | NXDOMAIN | Empty NOERROR |
|---|---:|---:|---:|---:|
| 512 KiB screen standalone A | 9,489 | 9,489 | 4,059 | 0 |
| 512 KiB screen standalone B | 9,502 | 9,502 | 0 | 3,644 |
| 512 KiB screen loaded A | 11,386 | 11,386 | 6 | 0 |
| 512 KiB screen loaded B | 11,477 | 11,477 | 0 | 36 |
| 4 MiB standalone A | 37,797 | 37,797 | 11,388 | 0 |
| 4 MiB standalone B | 37,744 | 37,744 | 0 | 14,788 |
| 4 MiB loaded A | 43,120 | 43,120 | 1 | 0 |
| 4 MiB loaded B | 41,598 | 41,598 | 0 | 3 |

All groups had **zero other RCODE, invalid reply, adapter error, and adapter
retry counts**. The model did intentionally drop new queries when full:
largest cumulative dropped counts were primary screen A 477 / B 458, and
sustained A 507 / B 344. Active workers reached 56 and queue depth reached 64.
These bounded-queue drops are modeled backpressure, not DNS ID misrouting.

There were **no ≥1-second backend receive gaps** in any corrected 512 KiB
sample or loaded 4 MiB sample. A's standalone 4 MiB sample had one gap of
1.024 s; B standalone maximum was .318 s. Loaded maximum gaps were primary
screen A .231 s / B .105 s, and sustained A .107 s / B .211 s. These are sample
maxima, not a percentile of every interval. One-second post-cleanup query
counts were screen A 112/100, B 77/115, sustained A 106, B 90: drain/recovery
observations only, not proof of persistent idle polling.

The NOERROR change is independently observed; it does not imply upload profit.
With reply ownership repaired and sufficient deadlines, these uploads complete.
The prior delayed failure pattern therefore cannot support ACK-starvation or
client-bottleneck claims. Corrected measurements expose no native cwnd,
ACK/MAX_STREAM_DATA or poll state; server/client cause remains unproven.

## Historical direct-loopback evidence retained

The former direct path had a single client peer and was unaffected by the
multiworker routing defect. It lacked acknowledged premeasurement warmup and
used a 4-second deadline, so these are historical observations, not a replacement
for the corrected primary screen. Results below show AB then BA.

| Direct UDP / load / binary | 32 KiB seconds | 128 KiB seconds | 512 KiB seconds | 4 MiB seconds |
|---|---|---|---|---|
| Standalone A | .020 / .023 | .070 / .117 | .261 / .354 | 2.408 / 2.635 |
| Standalone B | .120 / .021 | .074 / .071 | .297 / .294 | timeout / 2.557 |
| Loaded A | .033 / .033 | .099 / .111 | .431 / .437 | 2.988 / 3.138 |
| Loaded B | .026 / .040 | .097 / .108 | .360 / .430 | 2.983 / 3.053 |

Direct loaded summed time was A 7.2693 / B 7.0966 seconds (**2.37% shorter**).
All 16 direct loaded uploads were digest-acknowledged; one B standalone sample
missed its 4-second acknowledgement deadline despite full backend receipt.
A baseline A/A sample exhibited the same symptom. Native-cause interpretation
is unproven. Direct screen A standalone saw 36,436 NXDOMAIN replies; B saw
36,860 empty NOERROR replies and zero NXDOMAIN. These counters support the
response-format change, not a 20% performance gain.

## Artifacts, commands, and checks

Run from `/home/stfu/ai/work-table/space/tmp/slipstream-c`:

```sh
python3 -m unittest discover -s bench -p test_upload_first.py -v

# Corrected primary A/A then AB/BA; meaningful 60-second deadline.
python3 bench/upload_first.py --models tcp-delay-100ms --sizes 524288 \
  --deadline 60 --budget 1500 --output bench/upload-first-corrected512.jsonl

# Sustained checks, after primary completion, bounded at 180 seconds each.
python3 bench/upload_first.py --single-label A --single-pair sustained-1 \
  --single-order 0 --models tcp-delay-100ms --sizes 4194304 \
  --sustained-deadline 180 --budget 420 --output bench/upload-first-corrected4m.jsonl
python3 bench/upload_first.py --single-label B --single-pair sustained-1 \
  --single-order 1 --models tcp-delay-100ms --sizes 4194304 \
  --sustained-deadline 180 --budget 420 --output bench/upload-first-corrected4m.jsonl

python3 bench/upload_first.py --summarize \
  bench/upload-first-corrected512.jsonl bench/upload-first-corrected4m.jsonl
python3 -m py_compile bench/upload_first.py bench/test_upload_first.py
git diff --check
```

Both reply-ownership regressions pass. Syntax and whitespace checks pass.
Corrected native runs took roughly eight minutes in total, within the meaningful
configured bounds. `--sizes` and `--models` avoid broad redundant repetitions;
`--deadline`, `--sustained-deadline`, and `--warmup-deadline` are configurable.
The probe waits for a distinct candidate hash, protects against subsequent
candidate mutation between arms, and triggers six confirmation pairs only
after a failure-free ≥20% loaded screen with no new paired failures.

JSONL is ignored by existing `bench/.gitignore` and contains aggregate synthetic
counts/timings and binary hashes only. Output appends; use new filenames for
independent experiments. `probe_version: 2` identifies corrected rows. Do not
merge earlier delayed rows from `upload-first-measured.jsonl`,
`upload-first-clean.jsonl`, or `upload-first-final-arm.jsonl` into valid evidence.
`upload-first.jsonl` contains the excluded startup-flags failure. Historical
direct rows in the clean files remain usable with the limitations above.

The initially shell-aborted startup may have left a synthetic certificate
directory under `/tmp/upload-first-*`; the harness denied inspection/purge.
No token was stored there. All successful runs cleaned their temporary files.
The current ignored `.cache/` temporary path and SIGINT/SIGTERM handling
prevent recurrence on ordinary cancellation; authorized orphan cleanup remains
outstanding. No production/UOT, negative pin/token, actual recursive/LTE,
Telegram, or persistent-idle acceptance claim follows from this local probe.

## Same-model client checkpoint: current C versus unchanged Rust

**Decision: no ≥20% loaded gain; no six-pair confirmation or APK recommendation.**
The current C Linux client reduced loaded 512 KiB time in both fresh AB/BA
pairs, with a **9.14% median paired time reduction** (5.02% and 13.27%). The
single sustained 4 MiB loaded check was **11.28% shorter**. Standalone times
were almost equal. These bounded results do not reproduce the large difference
suggested by comparing a 100 ms UDP diagnostic with this TCP-DNS model.

This checkpoint changes only the client executable and its resolver CLI flag:
Rust uses `--authoritative`, C uses `--resolver`; both explicitly use `dcubic`.
The server, corrected carrier, fixed authenticated backend, synthetic payload,
and certificate validation are shared. Each arm has fresh native processes and
an acknowledged 1 KiB warmup. The same **56 workers / 64 queue / 100 ms modeled
query RTT** is used throughout. This is synthetic local Linux evidence, not an
actual recursive resolver or LTE measurement. No native source or client poll
patch was introduced by this workstream.

### Fixed binaries

| Role | Path | SHA256 |
|---|---|---|
| Fixed NOERROR server | `build-upload-noerror/slipstream-server` | `b9aedcd05127903f896a1716f45a621985d189e660f24220d0e6a48e8a832c75` |
| A: unchanged Rust Linux client | `../slipstream-rust-control-artifacts/slipstream-client` | `af32d5e5418a7ab2a3c807bf10c6229f13afe709a1ab07ec8c915768e95461f7` |
| B: current C Linux client | `build-candidate/slipstream-client` | `001c926c8ad26c8a3dc75b36402b8fdae281c8b618726e78df9b5608fb40f731` |

Every completed row records these actual hashes. Client-comparison mode checks
all three binaries against their initial hashes before each fresh arm and
bypasses the distinct-server wait. These A/B labels refer to **clients**, unlike
the server experiment above; do not pool those experiments.

### Completed uploads and concurrent download

Times include opening the authenticated upload stream and receiving the exact
SHA256 acknowledgement. Deadlines were 60 seconds for 512 KiB and 180 seconds
for 4 MiB. Download bytes and rates cover only the same loaded-upload interval;
rates below are decimal kB/s.

| Pair / execution order | Client | Standalone seconds | Loaded seconds | Same-interval download bytes | Download kB/s |
|---|---|---:|---:|---:|---:|
| Screen 1 / first | A Rust | 9.1280 | 9.7300 | 4,434,570 | 455.76 |
| Screen 1 / second | B C | 9.0249 | 9.2415 | 4,277,751 | 462.89 |
| Screen 2 / first | B C | 9.0261 | 9.2876 | 4,241,733 | 456.71 |
| Screen 2 / second | A Rust | 9.1365 | 10.7083 | 4,825,773 | 450.66 |
| Sustained retry / first | A Rust | 70.8151 | 81.8879 | 37,021,863 | 452.10 |
| Sustained retry / second | B C | 70.9532 | 72.6525 | 33,458,560 | 460.53 |

For the primary 512 KiB loaded screen:

- Median time: Rust **10.2191 s**, C **9.2645 s**; C wins **2/2** pairs.
- Paired reduction is `1 - C_time / Rust_time`; its two-pair median is **9.14%**.
  Summed-time reduction is **9.34%**. Neither reaches the 20% gate.
- Aggregate concurrent download rate: Rust **453.09 kB/s**, C **459.79 kB/s**.
  Lower total C download bytes reflect a shorter interval, not lower rate.
- Standalone summed-time reduction is only **1.17%**. The 4 MiB standalone
  C result is **0.20% slower**; its loaded win is one unconfirmed pair.

All **12 completed upload samples** acknowledged the full requested byte count
and exact digest. There were **zero upload deadline failures, download errors,
fixture validation errors, invalid DNS replies, or dead native processes**.
Seven warmups, including the interrupted arm, acknowledged 1 KiB in
0.471–0.476 seconds. No completed upload had a ≥1-second backend receive gap.
Largest gaps were Rust/C: standalone 512 KiB **0.316/0.319 s**, loaded 512 KiB
**0.213/0.225 s**, standalone 4 MiB **0.422/0.543 s**, loaded 4 MiB
**0.107/0.066 s**. These are upload receive gaps, not a download stall metric.

### DNS and bounded-carrier counters

Counters cover timed upload intervals. All replies had NOERROR; NXDOMAIN,
other RCODEs, invalid replies, adapter errors, and retries were zero.

| Phase | Client | Queries / replies | Empty NOERROR replies |
|---|---|---:|---:|
| 512 KiB standalone, two arms | Rust | 9,524 / 9,524 | 4,118 |
| 512 KiB standalone, two arms | C | 9,600 / 9,600 | 3,977 |
| 512 KiB loaded, two arms | Rust | 11,013 / 11,013 | 9 |
| 512 KiB loaded, two arms | C | 10,140 / 10,140 | 32 |
| 4 MiB standalone | Rust | 37,786 / 37,786 | 17,550 |
| 4 MiB standalone | C | 37,894 / 37,872 | 18,198 |
| 4 MiB loaded | Rust | 43,261 / 43,261 | 2 |
| 4 MiB loaded | C | 39,035 / 39,035 | 0 |

The C standalone 4 MiB interval ended with 22 queries still in flight; this
interval-boundary difference is not evidence of 22 lost replies. All bytes
were acknowledged. The carrier reached 56 active workers and queue depth 64.
Maximum cumulative drop-new counts within a fresh arm were screen Rust/C
**450/418**, sustained Rust/C **302/449**. They are deliberate bounded-queue
drops, not DNS identity failures; do not sum cumulative per-sample snapshots.
One-second post-cleanup query counts were screen Rust **86/115**, C **3/4**,
sustained Rust **95**, C **10**. This short drain/recovery observation suggests
a client behavior difference but establishes neither a persistent-idle rate
nor its causal effect on upload performance.

### Interrupted attempt, reproduction, and interpretation

The initial combined command hit the shell tool's **120-second external time
limit** during Rust's first standalone 4 MiB upload, after all eight screen
samples completed. This is an externally interrupted attempt, not an upload
deadline failure. Its acknowledged warmup and all completed screens remain in
`bench/upload-first-clients.jsonl`; the interruption is retained explicitly in
`bench/upload-first-client-interruption.jsonl` with unknown completion metrics.
No duration or partial-byte count is invented. A fresh A/B sustained pair is
retained in `bench/upload-first-client-sustained.jsonl` under
`sustained-retry-1`. No failed or interrupted artifact was overwritten.

The gate was calculated from the complete screen rows after that interruption;
it fails at 9.14%, so no confirmations ran. The reusable probe now emits its
screen gate before sustained checks, and ordinary cancellation emits an
in-progress sample marked `external_cancellation` before cleanup. The ignored
`.cache/` directory was empty afterward, and no probe/native processes remained.
This checkpoint took roughly seven minutes including the interrupted attempt.

```sh
# Reproduce the entire fixed-server checkpoint; allow the outer shell >=900s.
python3 bench/upload_first.py --compare-clients \
  --baseline build-upload-noerror/slipstream-server \
  --client ../slipstream-rust-control-artifacts/slipstream-client \
  --c-client build-candidate/slipstream-client \
  --deadline 60 --sustained-deadline 180 --budget 900 \
  --output bench/upload-first-clients-new.jsonl

# The actual fresh sustained retry commands used a 450s outer timeout each.
python3 bench/upload_first.py --single-label A --single-pair sustained-retry-1 \
  --single-order 0 --client-kind rust \
  --client ../slipstream-rust-control-artifacts/slipstream-client \
  --baseline build-upload-noerror/slipstream-server \
  --candidate build-upload-noerror/slipstream-server \
  --models tcp-delay-100ms --sizes 4194304 --deadline 60 \
  --sustained-deadline 180 --budget 450 \
  --output bench/upload-first-client-sustained.jsonl
python3 bench/upload_first.py --single-label B --single-pair sustained-retry-1 \
  --single-order 1 --client-kind c --client build-candidate/slipstream-client \
  --baseline build-upload-noerror/slipstream-server \
  --candidate build-upload-noerror/slipstream-server \
  --models tcp-delay-100ms --sizes 4194304 --deadline 60 \
  --sustained-deadline 180 --budget 450 \
  --output bench/upload-first-client-sustained.jsonl
python3 bench/upload_first.py --summarize bench/upload-first-clients.jsonl \
  bench/upload-first-client-sustained.jsonl bench/upload-first-client-interruption.jsonl
```

This model appears to impose a common transport/service bound on both clients:
nearly equal standalone results and saturated carrier workers are consistent
with that hypothesis, but do not prove a native server or client bottleneck.
The modest loaded C advantage is Linux parity evidence worth retaining, not a
large confirmed gain warranting an APK recommendation. Treating the unchanged
Rust Linux binary as equivalent to the current APK is **an explicit unverified
assumption**: Android scheduling, integration, real recursive/LTE carrier
behavior, and Telegram uploads were not tested. No Android speed promise,
APK build, or remote deployment follows from these results.

## Rust-only congestion-control checkpoint: DCUBIC versus BBR

**Decision: BBR fails the performance and download-loss gates. Stop after the
two fresh AB/BA screens; no six-pair confirmation or 4 MiB run.** BBR loaded
512 KiB uploads were **38.61% slower by median paired comparison**, losing
both pairs. Concurrent download rates fell **34.70% and 30.49%**, exceeding
the allowed 30% loss in each pair. These measurements provide no support for
a future APK BBR flag change in this model.

This is a separate campaign from both the server response-format and C/Rust
client comparisons. A means **Rust DCUBIC**, B means **the identical Rust
binary with BBR**. Only the experimental client congestion-control CLI changed.
The user identified the existing APK setting as DCUBIC in
`OwenclaveSlipstreamInstance.kt:247`; no Android setting or APK was changed.

Both labels used the unchanged client
`../slipstream-rust-control-artifacts/slipstream-client`, SHA256
`af32d5e5418a7ab2a3c807bf10c6229f13afe709a1ab07ec8c915768e95461f7`,
and fixed NOERROR server `build-upload-noerror/slipstream-server`, SHA256
`b9aedcd05127903f896a1716f45a621985d189e660f24220d0e6a48e8a832c75`.
The same corrected synthetic TCP-DNS carrier used 100 ms modeled query RTT,
56 workers and queue capacity 64. Fresh processes and an exact acknowledged
1 KiB warmup preceded every arm. Upload deadlines were 60 seconds.

| Pair / execution order | Rust CC | Standalone seconds | Loaded seconds | Same-interval download bytes | Download kB/s |
|---|---|---:|---:|---:|---:|
| Screen 1 / first | A DCUBIC | 8.9971 | 10.2978 | 4,750,796 | 461.34 |
| Screen 1 / second | B BBR | 13.7271 | 14.5870 | 4,394,460 | 301.26 |
| Screen 2 / first | B BBR | 12.8769 | 13.8093 | 4,399,098 | 318.56 |
| Screen 2 / second | A DCUBIC | 9.0216 | 10.1869 | 4,668,742 | 458.31 |

- Loaded paired time changes: BBR **41.65% and 35.56% slower**; median **38.61%**.
  Median loaded time was DCUBIC **10.2424 s**, BBR **14.1982 s**; BBR wins **0/2**.
- Standalone summed time: DCUBIC **18.0187 s**, BBR **26.6040 s**;
  BBR **47.65% slower**.
- Aggregate same-interval download rate: DCUBIC **459.83 kB/s**,
  BBR **309.67 kB/s**. Per-pair BBR/DCUBIC rate ratios were **0.6530 / 0.6951**.
- All **eight uploads** received exact SHA256 acknowledgements for **524,288
  bytes**, with full backend receipt. No upload deadline failures, download
  errors, fixture validation errors, invalid DNS replies, or dead native
  processes occurred. All four warmups acknowledged exactly 1,024 bytes:
  DCUBIC **0.468/0.474 s**, BBR **0.373/0.373 s**.
- DCUBIC standalone had **one ≥1-second backend receive gap** (maximum
  **1.179 s**). Other maxima: DCUBIC loaded **0.101 s**, BBR standalone
  **0.101 s**, BBR loaded **0.0056 s**; no other ≥1-second gaps. BBR's smoother
  receive gaps and faster tiny warmup do not offset its slower complete upload
  and lower simultaneous download rate.

### Carrier counters and recorded gate

All observed replies were NOERROR. NXDOMAIN, other RCODEs, invalid replies,
adapter errors and retries were zero.

| Timed intervals, two arms each | Queries / replies | Empty NOERROR replies |
|---|---:|---:|
| DCUBIC standalone | 9,487 / 9,487 | 3,771 |
| BBR standalone | 9,623 / 9,623 | 225 |
| DCUBIC loaded | 11,262 / 11,245 | 12 |
| BBR loaded | 10,350 / 10,352 | 0 |

Queries and replies are independently counted inside the timed interval:
in-flight traffic crossing interval boundaries can yield fewer or more replies
than queries without implying loss or duplication. Both reached 56 active
workers and queue depth 64. Maximum cumulative drop-new counts within an arm
were DCUBIC **501**, BBR **155**. Fewer bounded-queue drops and empty replies
are not evidence of improved end-to-end throughput. One-second post-cleanup
query counts were DCUBIC **88/76**, BBR **63/63**; no persistent-idle inference
is made.

Ignored `bench/upload-first-rust-cc.jsonl` retains all eight samples, four
acknowledged warmups, idle counters, actual hashes, `campaign: rust-cc`, each
requested congestion-control value, and the emitted `cc_gate` event. The event
records `failure_free_screen: true`, `confirmation: false`, paired time gains
**−0.416509 / −0.355593**, median gain **−0.386051**, and download-rate ratios
**0.653010 / 0.695079**. This campaign had no interrupted attempts and took
roughly two minutes.

The reusable probe adds only `--congestion-control {dcubic,bbr}` (default
`dcubic`) and `--compare-cc` using the existing arm scheduling. CC comparison
fixes the Rust binary and server, screens 512 KiB standalone/loaded in AB/BA
order, and requires failure-free ≥20% median paired loaded time reduction plus
both download-rate ratios ≥0.7 before any sustained or six-pair loaded
confirmation work. Distinct-server waiting is bypassed; hashes remain checked.

```sh
# Outer command timeout used: 950 seconds; internal budget: 900 seconds.
python3 bench/upload_first.py --compare-cc \
  --baseline build-upload-noerror/slipstream-server \
  --client ../slipstream-rust-control-artifacts/slipstream-client \
  --deadline 60 --sustained-deadline 180 --budget 900 \
  --output bench/upload-first-rust-cc.jsonl
python3 bench/upload_first.py --summarize bench/upload-first-rust-cc.jsonl
python3 -m unittest discover -s bench -p test_upload_first.py -v
python3 -m py_compile bench/upload_first.py bench/test_upload_first.py
git diff --check
```

Both reply-ownership regressions, syntax and whitespace checks pass. The
synthetic carrier's bounded service behavior may interact with congestion
control, but the measurements do not identify a specific native bottleneck.
The current local evidence favors retaining DCUBIC for this modeled path;
it establishes no general BBR ranking for actual recursive/LTE networks.
Rust Linux equivalence to the current APK remains an unverified assumption.
NOERROR correctness is independent of these performance results. No native,
Android, remote, goal, APK build or deployment changes were made here.
