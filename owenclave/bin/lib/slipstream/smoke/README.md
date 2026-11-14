# Android Slipstream Smoke

Foreground native screening **on the LTE phone**, without rebuilding or installing
an APK. Python >=3.11 in debuggable Termux is required; the tested Moto g54 uses
Termux Python 3.14.6, arm64 Android, `adb shell run-as com.termux`, no root or SELinux
changes. Host Python uses only its standard library. Unit tests additionally use
`openssl` for an ephemeral localhost certificate.

## Boundaries

- `host.py` owns one ADB run using a serial-specific advisory lock. Native mode
  requires Wi-Fi off, LTE, VPN stopped and zero carrier children. It extracts the
  ELF from `--apk` or uses `--native`; neither option installs an APK.
- `device.py` runs the native client in Termux, by default with two loopback UDP
  adapters, each 28 serial persistent TCP workers and 32 queued queries, drop-new. The accepted
  order is Safe `77.88.8.88:53`, Basic `77.88.8.1:53`; reverse is experimental.
  One child uses DCUBIC, authoritative endpoints and the unchanged carrier pin.
- Experimental `--topology third` adds Basic `77.88.8.8:53` as a third native path,
  workers19/19/18 and queues22/21/21. `--topology shard` keeps two native paths and
  splits the second adapter's28 workers equally between Basic .1/.8, sharing queue32.
  Retry stays worker-affine; QUIC sees pooled RTT/loss on that adapter. Both retain
  aggregate56/64. Equal budgets do not prove equal polling load or independent
  same-provider capacity. These are rejected research options, not APK profile modes.
- H14 `--domains old/new/split/split-reverse` assigns `t.x.ass-peak.de` and
  `tt.x.ass-peak.de` explicitly to the two accepted native paths; default `legacy`
  emits no mapping flag. Use the H14 ELF, not the accepted APK ELF, for these modes.
  Data/poll suffix counters are aggregates, not retained QNAMEs. The research patch
  `../multidomain-experimental.patch` applies after the accepted client patches,
  including bootstrap; it is deliberately NOT included by `build.sh`.
  H14 was rejected after confirmation and reversed-assignment screening, not deployed
  to Kotlin/APK. See the [H14 goal](../../../../docs/goals/2026-09-10-multidomain-sharding.md).
- H15 `--connections 2` is an explicitly isolated native-only experiment, not an APK
  mode. Two authenticated children have independent QUIC connections but share the
  SAME 56-worker/64-query adapters. `--partition` instead gives each connection two
  14-worker/16-query adapters, still56/64 aggregate. Same domain, pin and backend;
  the full workload assigns foreground to connection one and background DL to two.
  Only two payload flows run concurrently. Independent connection windows and two
  process runtimes add state; this is not an equal-CPU/memory production pool.
  One child remains the default and the APK invariant. Both layouts were rejected
  after loopback and Android LTE screens; no one-process pool was promoted.
- Flow token and generated SOCKS credentials enter native stdin only. Host secret
  JSON is a private `0600` file with `flow_token` (32 hex digits) and `fixture_token`
  (64 hex digits). Values travel via stdin, never argv/environment or phone files.
  Use separately provisioned test credentials for DE direct; production stays WARP.
- Native output is drained/discarded. Retained JSONL contains aggregates, artifact
  hashes, radio/battery and hashed egress identity, not payloads or credentials.
  Files are exclusive-create; failures and interruptions must not be overwritten.
- Host EOF and TERM cancel the device owner; native is terminated/reaped and adapter
  sockets/tasks closed. The host never edits profiles or starts/stops the VPN.
  In `gvisor` mode the caller owns the already-active VPN and must stop it afterward.
  Termux must be in the APK's captured app set. One child alone does not prove capture:
  independently verify HTTPS WARP/direct and transaction-checked UDP egress.
- Python differs from Kotlin: async whole-exchange deadlines and strict DNS ID/QR
  checking versus blocking per-I/O timeouts and no ID/QR check. Same budgets do not
  make scheduling or timings equivalent. A native gain is screening evidence only;
  build/install a credible candidate and repeat actual gVisor acceptance/throughput.

## Workload

Each window has fresh random, incompressible 8/32/128/512 KiB HTTPS uploads, first
sequentially, then each under one continuously replenished 1 MiB download. It also
has a sequential 1 MiB download and 4 KiB recovery. The fixture acknowledges exact
size and SHA-256, rather than merely returning HTTP 200. No Telegram/account data
or message API is used; these probes cannot prove that Telegram voice messages work.

`--workload short-parallel` separately tests two sequential32KiB uploads, then two
pairs of concurrent32KiB uploads and4KiB recovery, without background DL. Each pair
has aggregate completion time and exact acknowledged bytes. With two connections
the second flow uses connection two; a single serial flow is never striped.
Reports retain unexpected background errors separately from deliberate load
cancellation. `window.ok` still describes foreground payloads, not background health.
Aggregate DNS byte counters include headers and retransmissions, not goodput.

`connect_s`, `tls_s`, `submitted_s`, `first_byte_s` are cumulative timestamps from
request start; subtract adjacent timestamps for phase duration. With gVisor/local
SOCKS, connect success is not proof of the remote destination dial. DNS time is null
because the fixture uses a fixed IP, not an invented zero-latency DNS measurement.
`submitted` means handed to local socket buffers, NOT remotely received. Drain stall
metrics measure local backpressure only. Only exact acknowledgement contributes to
`ack_Bps`; failed/timeout requests contribute zero. Failed rows retain their phase
and elapsed time. Background cancellation is expected when its foreground upload
ends, and is explicitly recorded rather than counted as foreground failure.

Default request deadline is 40 s, native readiness 15 s, query age 10 s, up to two
exchange attempts with min(8 s, remaining age). There are at most two workload flows
and 56 adapter connections. Battery <=20% or >=42 C fails preflight. LTE availability
is checked, but restricted/allowlisted LTE requires a separate direct baseline.

## Fixture Lifecycle

`fixture.py` is a bounded authenticated HTTPS sink, **not a proxy**. It accepts at
most eight active HTTP handlers, 8 KiB headers, 512 KiB uploads and 1 MiB downloads;
TLS handshake timeout is 5 s, whole HTTP request timeout 45 s. Run only in an approved
ephemeral service with null stdout/stderr, private credential files, a short-lived
IP-SAN certificate and source-restricted firewall rule. Current direct identity is
DE `195.128.101.186`, fixture TCP `40004`; other hosts require changing that identity
check deliberately. `egress=warp` rejects DE direct, but still needs an independent
interface-bound WARP proof; a different source IP alone is not WARP proof.

Optional `--stats-file` writes one exclusive-create `0600` JSON on graceful shutdown:
TLS-completed handlers, authenticated requests, complete upload bodies, drained
replies, HTTP timeouts, I/O errors and bounded-close aborts. It retains no payload,
request identifier or peer address. These synthetic-only totals distinguish an
incomplete body from a missing application ACK; `reply_drained` is still a local
buffer boundary, not remote receipt. Pre-TLS failures are not counted. A close abort
alone does not prove payload loss. Leave this option off outside the bounded fixture.

DE operations own deployment, secret provisioning and teardown. For direct tests,
use optional FlowRelay dispatch cap16/backend32, not the confounded former cap8.
Production credentials, WARP binding, pin and firewall remain unchanged. WARP IPs
can vary by destination/flow; a single trace IP is not a stable fixture allowlist.
Do not broaden production routing to accommodate the test. Remove temporary units,
listeners, firewall rules, private keys and all local/remote credential copies at
the end. Neither fixture nor direct dispatch is currently deployed. The documented
dispatch backup anchor is `/root/backups/owenclave-h11-20260909T171047Z/`; it has original
production binaries/units/firewall, not usable test credentials. Deploying/removing
the dispatch override restarts FlowRelay **and** its dependent Slipstream service;
WARP is not restarted. The restored original FlowRelay predates `-check`: verify
its original hash/unit and bounded namespace-loopback startup, not unsupported flags.
H14's unit/firewall-only anchor is `/root/backups/owenclave-h14-20260909T215102Z/`;
its validated second server suffix is retained, not automatically rolled back on
client rejection. H14 used unchanged production WARP for both arms, not direct egress.
Its fixture, rule, private key and host/phone credential copies have been removed.

## Commands

From the Owenclave root, after explicit fixture/credential provisioning:

```sh
python3 -m unittest discover -s bin/lib/slipstream/smoke -p test_smoke.py -v
python3 bin/lib/slipstream/smoke/host.py --help
python3 bin/lib/slipstream/smoke/pair.py \
  --serial "$ANDROID_SERIAL" --control build/dns-multipath/fixed.apk \
  --candidate "$CANDIDATE_ELF" --candidate-native --pair 1 --label upload-screen \
  --secret-file "$PRIVATE_TEST_JSON" \
  --cert app/src/main/res/raw/slipstream_server.crt \
  --fixture-cert "$PUBLIC_FIXTURE_CERT" --fixture-host 195.128.101.186 \
  --output-dir build/dns-smoke
```

Run pair 2 for the reverse CT/TC order. Use a new label and fresh balanced pairs for
confirmation (initially six), not reused screening windows. To re-screen H6 add
`--candidate-order reverse` with the same control APK as candidate. For A/A
reproducibility supply the control APK as both inputs, without `--candidate-native`.
For H9/H10 use that same APK as both inputs and `--candidate-topology third` or
`--candidate-topology shard` respectively. Topology/order/native identity may differ
between arms, as may H14 domain layout; each arm's manifest must remain fixed
throughout a report. Use individual `host.py` windows with `--domains` for H14;
the pair wrapper does not select domain layouts.

Retained R&D reports need no live fixture or credentials:

```sh
python3 bin/lib/slipstream/smoke/report.py --directory build/dns-smoke --label h9-screen --pairs 2
python3 bin/lib/slipstream/smoke/report.py --directory build/dns-smoke --label h10-screen --pairs 2
python3 bin/lib/slipstream/smoke/report.py --directory build/dns-smoke --label h10-confirm --pairs 6
```

```sh
python3 bin/lib/slipstream/smoke/report.py \
  --directory build/dns-smoke --label upload-confirm --pairs 6
python3 bin/lib/slipstream/smoke/host.py --mode gvisor \
  --serial "$ANDROID_SERIAL" --secret-file "$PRIVATE_PRODUCTION_JSON" --egress warp \
  --cert app/src/main/res/raw/slipstream_server.crt \
  --fixture-cert "$PUBLIC_FIXTURE_CERT" --fixture-host 195.128.101.186 \
  --output build/dns-smoke/apk-confirm-unique.jsonl
```

The second command requires the intended APK/profile already connected. Its manifest
order/topology/path-layout/worker fields describe native harness defaults, not discovery of the APK's
Automatic/single/MP profile; record that profile separately. Confirm installed APK
hash in preflight. Stop VPN and verify zero children after each acceptance window.

The report rejects incomplete harness windows and manifest drift, retains startup
and request failures, and reports per-size p50/p95/max, paired wins/ranges and a
deterministic 10,000-resample median bootstrap interval. A failed control gives an
undefined ratio, never an infinite gain; its failure remains listed. A failed
candidate with a successful control gives -100%. Small samples and carrier drift
make intervals descriptive, not universal confidence claims. No arbitrary >=10%
gate: repeatable modest upload/completion gains matter, but new startup failures
or short-upload regressions cannot be hidden by bulk medians.

## Local Causal Probe

`causal.py` uses the existing Linux client/server, two synthetic UDP adapters
(28 workers/32 queued each), one fixed authenticated loopback bridge and the same
pinned HTTPS workload. It does NOT use Android, real recursive DNS, real FlowRelay
or WARP. Synthetic service delay is not a faithful model of TCP resolver capacity.
OpenSSL creates ephemeral credentials/certificate in a temporary directory; native
output is discarded. No phone, public fixture or production secret is needed.

```sh
python3 bin/lib/slipstream/smoke/causal.py \
  --client build/dns-multipath/host-fixed/release/slipstream-client \
  --server build/dns-multipath/host-fixed/release/slipstream-server \
  --delay .08 .4 --windows 2 --output build/dns-smoke/causal-new.jsonl
```

Use a new output path for each run. Default mode executes the full workload. Add
`--connections 2` for independent clients sharing the DNS adapters; add `--partition`
to reserve half of the same56/64 budget for each client. `--cc bbr` changes only
client congestion control, not the server. `--workload short-parallel` selects the
separate concurrent-upload workload and cannot be combined with `--focused`. Add
`--focused` for only loaded512KiB plus recovery, with aggregate bridge progress every
5 seconds (at most eight samples). Add `--release-load-after 20` only as a causal
intervention: stopping DL changes offered load and MUST NOT be reported as a
same-workload speed gain. Bounds: 1..6 windows, two delays0..0.5s, release0..30s,
readiness15s, request40s, fixed bridge cap8. The exit code indicates experiment
execution, NOT successful payload acceptance: inspect every `window.ok`, request
failure and cleanup row. SIGINT/SIGTERM cancellation reaps the server and all clients and
closes owned sockets/tasks; interrupted evidence remains exclusive-create.

2026-09-09 H13: eight valid local windows, one Android-native window and three
accepted APK/gVisor windows executed. Local asymmetry reproduced an incomplete-body
ACK timeout without the external layers; focused intervention showed continued
progress and load competition, not a proven global deadlock. Phone results were
29/32 exact upload ACKs, so production payload acceptance remains red. No performance
code or APK adopted. Fifteen unit tests passed, including phase-counter auth/partial
body tests and two-child cancellation; real local TERM cleanup also passed.
Full results: [throughput goal](../../../../docs/goals/2026-09-09-multipath-throughput.md).
