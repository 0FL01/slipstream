# Native parity gates

Configure and build with Meson, then run `meson test -C <build-dir> --print-errorlogs`.
The runtime test depends on both executables, so Meson rebuilds them before testing.
Python 3 and OpenSSL's CLI are required; all services, certificates and credentials
are ephemeral and confined to loopback. Native stdout/stderr must stay empty.

The six suites cover:

- `protocol`: exact 80-byte stdin credentials, fragmented SOCKS authentication,
  FlowRelay TCP and UoT framing, scheduler thresholds, and forced partial socket
  writes with delayed FIN, sparse-poll retry deadlines and the Warm transition.
- `path-guard`: invalid forced-path indices, missing remote CID, path compaction,
  and deletion of the requested path against the actual patched picoquic.
- `pin`: the actual certificate/signature callbacks with valid RSA-PSS,
  RSA-PKCS1, EC and Ed25519 signatures; tampered signatures, changed transcripts,
  unsupported algorithms, missing signature bytes and mismatched leaf DER fail.
- `server-codec`: valid zero-QUIC DNS replies retain NOERROR and no answers;
  invalid suffix/type/Base32 retain NXDOMAIN, with question/flags/EDNS preserved.
- `server-feedback-diag`: positive control for the diagnostic observer: the real
  pacing gate rejects the seventeenth same-time full-sized packet, records the
  pending ACK, and refills its default bucket after one microsecond.
- `runtime`: both server suffixes, independent concurrent streams, exact payloads,
  512 KiB upload followed by response after local FIN, remote-first FIN with the
  upload half still open, authentication/token/pin rejection, unchanged UoT
  records, bounded setup/bootstrap, malformed DNS, connection-slot reclamation,
  multipath loss and quiet/empty polling bounds.

For lifecycle checks, use a separate debug build with
`-Db_sanitize=address,undefined -Ddefault_library=static`. Sanitizer failures are
reported as stack traces; synthetic payloads and credentials are never printed.

The client requires `--cert <pinned-leaf.pem>` in raw and FlowRelay modes.
`--flow-relay-stdin` consumes token16 + username32 + password32 and EOF; username
and password are lowercase hexadecimal ASCII. Its listener must be `127.0.0.1`.
The setup deadline is 5 seconds, bootstrap is at most 3 seconds per endpoint,
and each transport owns at most 64 local flows. Each flow has a 128 KiB receive
window and a bounded 256 KiB output buffer; receive credit advances only after
socket writes. Tracked picoquic patches are applied idempotently during Meson
configuration and reject unexpected source revisions.

These gates establish local C/C correctness. Cross-implementation interoperability,
recursive carrier performance and benefit relative to Rust belong to the separate
A/B campaign.

## Server feedback reachability

Build the separate synthetic diagnostic server explicitly:

```sh
meson compile -C /work/build-candidate test-server-feedback
python3 tests/probe_server_feedback.py build-candidate ../slipstream-rust-control-artifacts/slipstream-client
python3 tests/probe_server_feedback.py build-candidate ../slipstream-rust-control-artifacts/slipstream-client --delay-ms 100 --capacity 16384
```

`test-server-feedback` links the production server sources with test-only wrappers
around the real pacing authorization, packet preparation and application-credit
calls. It emits one aggregate JSON object on clean shutdown; the production
`slipstream-server` has no wrappers or diagnostic output. The probe reports hashes,
acknowledged 512 KiB and 4 MiB uploads, standalone and with concurrent
downloads, plus actual pacing denials, empty preparations with primary ACK/misc
pending, and credit advances. It does not alter transport parameters or frames.

The bounded synthetic UDP carrier preserves an independent upstream socket for
each downstream peer, and each delayed reply retains its original peer. Queued
datagrams exist only in memory; capacity drops and download errors fail the probe.
It models delay, not recursive caching or worker limits; timings are diagnostic
reachability evidence, not paired performance acceptance. Rust client native
output is discarded; neither payloads, DNS names nor credentials are printed.

For the bounded client-gate checkpoint, build the separate diagnostic client:

```sh
meson compile -C /work/build-candidate test-client-feedback
python3 tests/probe_server_feedback.py build-candidate build-candidate/test-client-feedback --c-client --delay-ms 100 --capacity 16384
python3 tests/probe_server_feedback.py build-candidate build-candidate/test-client-feedback --c-client --delay-ms 100 --capacity 16384 --loss-percent 2
```

This adds aggregate real pacing denials, cwnd-blocked observations, peak bytes
in flight, requested/empty/emitted polls and maximum ACK-progress gaps while
bytes remain in flight. Pacing checks can observe the same state repeatedly;
their counts are not packet counts. Explicit preparation counts cover scheduled
polls, while the pacing observer also covers normal STREAM packet preparation.
The client wrapper does not change gates or frames and exists only in the
explicit test target. Loss is independently sampled per datagram in either
direction with a fixed seed. Each session bounds admitted DNS operations to
200000 by default, queue capacity to the supplied limit, and each acknowledged
upload to a 45-second socket timeout. Capacity/operation-limit drops fail the
fixture; intentionally sampled loss is reported separately.

For a local, synthetic credit-feedback probe (not the recursive A/B runner):

```sh
python3 tests/probe_throughput.py <build-dir> --delay-ms 100 --samples 3
```

The probe downloads exactly 1 MiB through a DNS relay delaying responses by
100 ms, with a bounded queue of 1024 responses. It emits aggregate elapsed time,
goodput, query count and peak pending responses, without traffic metadata.
Receive-credit tests verify early replenishment and that advertised credit never
exceeds the existing window beyond data drained to the socket. Compare immutable
candidate artifacts separately; this fixture does not establish DE benefit.

A bounded worker/queue fixture can also measure a 512 KiB acknowledged upload
while a second flow repeatedly downloads 1 MiB:

```sh
python3 tests/probe_throughput.py <build-dir> --delay-ms 100 --samples 3 \
  --loaded-upload 524288 --workers 56 --queue 64
```

`--workers` bounds outstanding DNS operations and `--queue` bounds waiting
requests; excess requests are dropped and counted. This is a synthetic UDP
fixture, not a real recursive resolver or the main TCP-DNS adapter. An optional
`--baseline-build <immutable-build-dir>` measures C0 with a raw FlowRelay OPEN;
C0 diagnostic output is discarded, while candidate output remains a strict gate.
