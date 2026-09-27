# Native parity gates

Configure and build with Meson, then run `meson test -C <build-dir> --print-errorlogs`.
The runtime test depends on both executables, so Meson rebuilds them before testing.
Python 3 and OpenSSL's CLI are required; all services, certificates and credentials
are ephemeral and confined to loopback. Native stdout/stderr must stay empty.

The four suites cover:

- `protocol`: exact 80-byte stdin credentials, fragmented SOCKS authentication,
  FlowRelay TCP and UoT framing, scheduler thresholds, and forced partial socket
  writes with delayed FIN, sparse-poll retry deadlines and the Warm transition.
- `path-guard`: invalid forced-path indices, missing remote CID, path compaction,
  and deletion of the requested path against the actual patched picoquic.
- `pin`: the actual certificate/signature callbacks with valid RSA-PSS,
  RSA-PKCS1, EC and Ed25519 signatures; tampered signatures, changed transcripts,
  unsupported algorithms, missing signature bytes and mismatched leaf DER fail.
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
