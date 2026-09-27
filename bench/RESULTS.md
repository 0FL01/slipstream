# Experimental C parity results — 2026-09-27

This branch is **not a production promotion**. The confirmed benefit is limited
to the preselected transport-only download scenario against unchanged C0.
The full FlowRelay/WARP loaded-latency gate failed; no full-path performance
acceptance or superiority over Rust is claimed.

## Provenance and method

- C0 source: `397850b16e70a52513269134b2c7b9aa13a56745`, picoquic
  `2ee2ecd189354791e15cc1ff9db86e4e4b8eba10`.
- Rust source: `bc772dd07d9a136dbd7553b0da575526de207847`, with accepted
  adaptive-idle, flow-relay, battery-scheduler, multipath-bootstrap and
  picoquic-multipath patches; independently built client, preserved deployed server.
- C2 client SHA256: `4f0ff1161c3c1e929f8017469d6d722ff9fe8c73c128090a17b7955a90941770`.
- C2 server SHA256: `43ffbf09057105f6bda35ded7fda79749e25a2c15b34894891aa60270fc04c38`.
- Debian 12 release builds, LTO, DCUBIC, local Python runner, one recursive TCP
  DNS adapter with 56 workers/64 queue slots, unchanged main suffix and backend
  per comparison. Raw C0 is isolated by the loopback-only bind shim.
- Primary: remotely acknowledged, digest-checked sequential 1 MiB download.
  Four upload sizes, two parallel downloads, uploads under continuous download,
  idle/recovery, CPU/RSS/threads and DNS counters were also measured.
- Fresh A/A primary variation: C0 0.85%, Rust 3.32%. Two AB/BA screening pairs;
  six fresh alternating AB/BA transport confirmation pairs. Failed requests
  remain in evidence; intentional background cancellations are separate.

## Results

| Scenario | Paired-median primary change | Wins | Loaded upload p95 change | Decision |
|---|---:|---:|---:|---|
| Transport C2/C0, six confirmation pairs | +21.96% | 6/6 | +6.58% | Scenario-specific gates pass |
| Transport C2/Rust, two screen pairs | -3.84% | 0/2 | -55.50% | No primary superiority |
| FlowRelay/WARP C2/C0, two screen pairs | +11.07% | 2/2 | +24.26% | Loaded-latency gate fails |
| FlowRelay/WARP C2/Rust, two screen pairs | -7.69% | 0/2 | -39.73% | No primary superiority |

Transport confirmation ratios were 1.413295, 1.304601, 1.034003, 1.200656,
1.238587 and 1.116145. The median of paired ratios is not the ratio of arm
medians (301026 B/s C0, 350759 B/s C2). Loaded p95 was 11.5605 -> 12.3211 s;
the corresponding WARP C0 comparison was 11.9005 -> 14.7873 s. Including failed
rows and successful-only p95 happened to produce identical values here.

All twenty fresh primary requests passed. C2 had zero unexpected payload
failures across ten fresh confirmation/WARP windows; C0 retained seven loaded
upload failures in confirmation and nine parallel/background failures in WARP
screening. Rust's corrected WARP windows had no unexpected failures. A successful
primary does not make the entire C0 window successful.

Transport median native CPU per window increased from 1.445 to 1.90 s; peak RSS
was about 9.79–10.04 MB versus 9.20–9.59 MB, four threads versus one. C2's quiet-open
query rate increased (~19.93/s versus ~5.57/s); settled empty polling decreased
(~0.60/s versus ~6.60/s). **No overall idle/CPU reduction over C0 is claimed.**

## Correctness and unsuccessful iterations

- Release and ASan/UBSan: four suites pass, including 12 runtime cases;
  Python harness: nine tests pass. Rust: 49 unit tests and DNS vectors pass;
  two unchanged upstream Clippy style warnings remain.
- Real pinned/authenticated TCP through FlowRelay/WARP acknowledged 8192 bytes;
  native UoT echoed an exact authenticated 99-byte datagram. Wrong local password,
  Flow token, carrier pin and UDP fixture token were rejected. Dead-primary/live-
  secondary bootstrap passed; both-dead exited fail-closed.
- Both server suffixes passed direct authoritative acceptance. Secondary recursive
  acceptance failed: authoritative zone query subsequently proved that the
  secondary suffix has no NS delegation (NXDOMAIN), while the main suffix does.
  DNS zone changes are outside this branch; direct server acceptance does not
  erase this pre-existing recursive-path limitation.
- Cross-pairs: Rust client/C0 server worked; C0 client/Rust or C2 server failed;
  C2 client/C0 server failed recovery. They were not used for attribution.
- Initial C1 regressed. Advancing bounded receive credit after 16 KiB rather than
  64 KiB drains improved the local delayed-relay download about 37%, producing C2.
  Busy burst 16->40, Rust-equivalent credit defaults, and removing the poll target
  cap did not fix loaded upload in the local fixture and were reverted.
- Initial WARP fixture startup failed on every variant because an exact source-IP
  firewall rule became stale as WARP changed source per connection. A temporary
  authenticated, fixed-destination fixture restricted to the RDAP-verified
  Cloudflare provider network corrected the experiment; failed rows were retained.

These are local Linux/DE measurements, **not LTE results**. Native output, qlog
and key logging stayed disabled. Ephemeral credentials and raw JSON evidence are
excluded from Git. Permanent service migration and acceptance of the WARP latency
trade-off require a separate decision.
