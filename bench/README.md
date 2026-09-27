# Local Slipstream A/B harness

Python 3.11+, standard library only. All output is aggregate-only JSONL (counts,
timing, artifact hashes, secret-free experiment labels). Native stdout/stderr go
to `/dev/null`; Docker uses `--log-driver none`. No QNAMEs, peer addresses, token,
SOCKS credentials, native output or payload are written. No remote commands are
issued. Each invocation measures the server variant the operator has already
selected; `--server` records that label, it does not switch anything.

## Fixed target to deploy separately

```sh
python3 /opt/slipstream-ab/backend.py --port 40100
```

Deploy `bench/backend.py` as that path. It binds only `127.0.0.1:40100`, never
forwards, allows eight connections and a maximum 1 MiB command, and has no normal
output. Set service stdout/stderr to null. Protocol: `AB01`, one ASCII opcode
`D`/`U`/`E`, network-order uint32 size. Payload is exactly repeated `x`.
`D` sends N bytes, reads 32 raw SHA256 bytes and returns `OK` + raw digest;
`U` reads N bytes then returns `OK` + raw digest; `E` echoes N bytes. No EOF is
required to finish a command. One command per connection; FIN directions are
independent. The transport experiment must route to this same fixed target for
all variants. C0's server may forward directly to it; C1/R SOCKS needs the
matching FlowRelay backend restricted to the target. With a Flowd backend on C0,
use `--baseline-flow-open` to prepend the matching OPEN in Python.

## Credentials and clients

Pass `--credentials /protected/credentials.json` (owned by current user, regular
file, mode 0600; symlinks rejected) or `--credentials -` to read one bounded JSON
line from stdin. Schema: `{"flow_token":"<32 hex chars>"}`. HTTPS fixture mode
also needs `"fixture_token":"<64 hex chars>"`. These placeholders are not real
secrets. The runner generates fresh 32-character local username/password and
writes exactly token16 + username32 + password32 bytes to the child stdin, then
closes it. Credentials never enter child argv or environment.

Default pinned binaries, relative to the operations workspace's `tmp/`:

* C0: `slipstream-c-baseline/build/slipstream-client` (raw TCP, no pinning).
* C1: `slipstream-c/build-candidate/slipstream-client` (authenticated SOCKS).
* R: `slipstream-rust-control-artifacts/slipstream-client` (authenticated SOCKS).

`--binary` overrides the artifact for an incremental candidate. `--cert` supplies
the public carrier PEM for C1/R. All clients explicitly use DCUBIC. Auto execution
probes `--help` with null output and falls back to image
`slipstream-ab-build:bookworm` when native execution is incompatible. Docker uses
host networking, host PID visibility for `/proc` measurements, read-only artifact
and certificate mounts, stdin credentials and no retained logs. Force it with
`--execution docker`. C0's unchanged listener binds all host addresses: this is
an explicit baseline limitation. No production/private credentials are needed
for a C0 direct-backend run.

## Single measurement and carrier budgets

From `tmp/slipstream-c`:

```sh
python3 bench/run.py --variant C0 --server C0-release --results bench/results.jsonl \
  --carrier direct --campaign c1-c0 --phase aa --arm A --samples 2

python3 bench/run.py --variant C1 --server C1-release --results bench/results.jsonl \
  --credentials /protected/credentials.json --cert /public/carrier.pem \
  --carrier recursive --campaign c1-c0 --phase screen --pair 0 --position 1 --arm B
```

The two examples demonstrate syntax; a campaign **must keep carrier, backend,
domain, cert, deadlines and budgets identical across arms**. Defaults:

* `direct`: UDP `195.128.101.186:53`, no adapter. Adapter query counts/rates are
  unavailable (`null`), rather than inferred as zero.
* `recursive`: TCP Yandex Safe `77.88.8.88:53`, 56 workers/64 queue.
* `multipath`: independent TCP Safe `77.88.8.88` and Basic `77.88.8.1`, each
  28 workers/32 queue, one native client. C uses repeated `--resolver`; Rust uses
  repeated `--authoritative` for the loopback adapter endpoints.

Adapter implementation derives from Owenclave `smoke/device.py`: persistent
serial exchange per worker, bounded drop-new queue, DNS size 12..4096, strict
matching ID and QR, 10s total age, at most 8s per attempt, one retry. Statistics
retain counters only. CLI `--direct-dns IP:port` supports controlled local testing.

Workloads in each fresh-client sample: uploads 8/32/128/512 KiB, 1 MiB download,
two concurrent 1 MiB flows, each upload size under replenished 1 MiB download,
5s empty idle / 15s quiet-open idle / fresh 8 KiB recovery / 5s empty settle.
An exact 32-byte startup exchange is outside measured workload rows. For explicit
FIN correctness screening, add `--half-close` (identically across arms). Default
request timeout is 60s and aggregate startup timeout 15s. Submitted upload bytes
are distinct from remotely acknowledged bytes. Failed/time-out/cancelled rows
have zero goodput; background cancellation is labeled and counted separately
from an unexpected background failure. Per-window CPU seconds, RSS/thread start,
end and deltas, sampled peaks (200ms), adapter totals and idle deltas are recorded.
Resources describe the native client PID, not the Docker CLI; adapter overhead is
shared harness overhead rather than attributed native CPU. Completion latencies
include connect/auth and remote ACK; timeout counts remain separate.

Optional `--backend https --target-host IP --target-port 40004 --fixture-cert PEM
--egress warp` uses the existing Owenclave `smoke/fixture.py` protocol, including
Bearer auth, pinned inner TLS, remotely acknowledged uploads, fixture version,
download digest and expected direct/non-direct source flag. It accepts synthetic
`x` uploads; that existing fixture's downloads are random but digest-checked.
The source flag alone is not an independent WARP-interface proof.

## Frozen ordering and summary

```sh
python3 bench/run.py --schedule --phase aa
python3 bench/run.py --schedule --phase screen
python3 bench/run.py --schedule --phase confirm
python3 bench/summarize.py bench/results.jsonl --primary sequential:D:1048576
python3 -m unittest discover -s bench -p 'test_*.py' -v
```

A/A: two fresh identical A samples (`--samples 2` only for `--phase aa`), inspect
dispersion before comparing variants. Screens: pair0 AB, pair1 BA. Confirmation:
six **fresh** pairs AB/BA/AB/BA/AB/BA (ABBAABBAABBA). Main switches servers between
invocations and supplies the printed `--pair`, `--position`, `--arm` values;
`--sample` is an optional replicate label. A is control, B is candidate. Use
separate `--campaign c1-c0` and `--campaign c1-r`; never pool the controls. Run
separate carrier/backend campaigns with the same frozen primary selected before
results. The summarizer preserves candidate timeout ratio zero, marks ratios
undefined for failed controls, and reports paired median and wins. Throughput
gate is six valid pairs, median B/A >=1.10, at least five wins, no candidate
payload failures. Final acceptance additionally needs loaded p95 regression
<=10%, auth/pinning/correctness checks and resource/idle review. An idle-cost
benefit is a separate decision from that throughput gate.

Exit codes: 0 all foreground requests and unexpected background requests passed;
1 measured/startup failure; 2 invalid input; 130 cancellation. Always retain failed
JSONL rows. SIGINT/SIGTERM clean up owned native/container processes and adapter
workers. Results append, so use fresh files/campaigns and unique pair/sample labels
to avoid ambiguous duplicate evidence. This harness makes no remote state changes.
