# Native parity gates

Configure and build with Meson, then run `meson test -C <build-dir> --print-errorlogs`.
The runtime test depends on both executables, so Meson rebuilds them before testing.
Python 3 and OpenSSL's CLI are required; all services, certificates and credentials
are ephemeral and confined to loopback. Native stdout/stderr must stay empty.

The suites cover:

- `protocol`: exact 80-byte stdin credentials, fragmented SOCKS authentication,
  FlowRelay TCP and UoT framing, scheduler thresholds, and forced partial socket
  writes with delayed FIN, sparse-poll retry deadlines and the Warm transition.
- `path-guard`: invalid forced-path indices, missing remote CID, path compaction,
  and deletion of the requested path against the actual patched picoquic.
- `poll-submit`: production-loop packet submission/error/cleanup cases. A scripted
  native clock advances during the empty regular attempt and encoding; subsequent
  regular/POLL prepares must see that elapsed time, while existing batch-based
  poll budgets and sparse deadlines remain unchanged. This is not a pacing benchmark.
- `pin`: the actual certificate/signature callbacks with valid RSA-PSS,
  RSA-PKCS1, EC and Ed25519 signatures; tampered signatures, changed transcripts,
  unsupported algorithms, missing signature bytes and mismatched leaf DER fail.
- `stream-ack-ranges`: serialized STREAMs through the production packet ACK
  walker/STREAM ACK handler and retransmission classifier. Empty non-FIN frames
  at zero, positive and maximum offsets are consumed without adding byte ACKs;
  sparse ranges, duplicates, adjacency, FIN markers, data+FIN, implicit/explicit
  lengths, the 62-bit end boundary and malformed/truncated headers are checked.
- `server-dns`: actual server decode/encode through SPCDNS wire bytes; in-zone
  IN-class NS/A/AAAA (and other non-TXT) return authoritative NOERROR/NODATA at
  apex and label-boundary subdomains, with case/trailing-dot variants. Non-IN non-TXT,
  outside/impostor names, TXT positive/error and malformed-query behavior,
  IDs, question echo, addresses and current OPT semantics are checked. Successfully
  decoded in-zone TXT with no prepared QUIC reply returns NOERROR/NODATA (zero
  answers, no empty TXT RR), consistent with A/NS existence for the same QName;
  invalid encoding and outside-zone TXT retain NXDOMAIN and existing class handling.
  A strict RFC9156-style walk must reach a descendant TXT instead of cutting on ancestor
  NXDOMAIN, while the same descendant is also checked directly.
- `runtime`: both server suffixes, independent concurrent streams, exact payloads,
  512 KiB upload followed by response after local FIN, remote-first FIN with the
  upload half still open, authentication/token/pin rejection, unchanged UoT
  records, bounded setup/bootstrap, malformed DNS, connection-slot reclamation,
  multipath loss and quiet/empty polling bounds. The low-rate three-stream
  idle-to-active bytes/FIN gate also runs through a small strict QName-minimizing
  relay that probes the zone apex and one-label-below-zone before forwarding
  actual client TXT requests; TLS and the real client/server are exercised.

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
A/B campaign. The strict QName relay is a synthetic continuation/cut fixture,
not a full recursive resolver or evidence of the real 180-second call gate.

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

## STREAM ACK range regression

The tracked `picoquic-stream-ack-range.patch` leaves valid empty non-FIN STREAMs
as no-ops after parsing/consumption. It rejects an end offset above `2^62-1`
with the existing `-1` result before SACK or stream-lifetime mutation. Nonempty
data keeps inclusive byte ranges; FIN keeps its inclusive end-offset marker.

The native reproducer first ACKs `[0,4)` and `[5,8)` and verifies that byte 4
still needs retransmission. Empty non-FIN at offset 5 is a preservation control:
it already leaves this particular hole intact before the fix. Empty at offset 0
was the suppression reproducer: its inclusive end underflowed to `UINT64_MAX`,
merged the ranges and made the production classifier skip byte 4. On a fresh
stream, positive-offset empty frames also created an invalid inverted range.
The unpatched handler accepted out-of-domain ends. These are ACK bookkeeping
regressions, not evidence of an empty-frame occurrence or a device outage.
The normal queued/active `picoquic_format_stream_frame` producer drops zero-length
non-FIN output; its inspection does not establish a live occurrence.

Run the narrow gate and all native gates on an isolated source export:

```sh
# For an archive export inside another Git repository, isolate git-apply paths.
git -C <exported-source-dir>/subprojects/picoquic init --template=
meson setup <build-dir> <exported-source-dir>
meson test -C <build-dir> stream-ack-ranges --print-errorlogs
meson test -C <build-dir> --print-errorlogs
meson setup <sanitizer-build-dir> <exported-source-dir> --buildtype=debug \
  -Db_sanitize=address,undefined -Ddefault_library=static
meson test -C <sanitizer-build-dir> stream-ack-ranges --print-errorlogs
```

The regression uses only synthetic in-memory frames and loopback connection
addresses. Configure applies all tracked patches only to that export; no raw
dependency checkout needs editing. This gate does not change transport pins,
TLS/pinning, media, congestion control, recovery policy or scheduler thresholds.
