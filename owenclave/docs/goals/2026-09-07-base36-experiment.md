# Base36 DNS codec experiment

Status: complete — codec gain measured; wire-format A/B not justified
Source: user-requested Stage 2 Base36 experiment after commit/push
Date: 2026-09-07

## Scope and decision rule

Compare the pinned Slipstream Base32 QNAME codec with the exact MasterDNS lowercase
Base36 block codec. Check round-trip behavior, leading zero bytes, case handling,
DNS label/name boundaries, codec CPU and actual query wire sizes for the active
domain `t.x.ass-peak.de`. Build a compatible client/server pair only if the measured
capacity/CPU trade-off justifies a new wire format. No active source, APK, server,
resolver, TLS or FlowRelay was changed during this experiment.

Chosen narrow threshold: a small codec saving must be weighed against a client/server
wire migration. A maximum payload gain around one percent with materially slower
mobile encoding is not sufficient evidence for a public LTE A/B. A synthetic codec
round-trip is required before any future pair.

## Implementations checked

- Slipstream pin `bc772dd07d9a136dbd7553b0da575526de207847`, picoquic/DNS source
  from the existing accepted cache. Base32 uses uppercase `A-Z2-7`; QNAME labels
  are split every 57 characters; current DNS name limit is 253 presentation chars.
- MasterDNS pin `acbf1c61f90786f41b975d2e2f616afbce292b29`,
  `internal/basecodec/lowerbase36.go`. Base36 encodes fixed blocks of 7 bytes to
  11 lowercase alphanumeric characters, with remainder sizes 1..6 mapped to
  2,4,5,7,8,10 characters. Decode accepts uppercase as well as lowercase.
- Cross-language vectors from 0, 1..7, 16, 32, 64, 128, 145 and 148 bytes matched
  exactly between the Rust port and MasterDNS Go implementation. Vectors contain
  leading zero bytes; uppercase Base36 decode round-tripped. Rust boundary checks
  enforce 253-character names and 57-character labels.

## Capacity and wire size

The query calculation includes the DNS header (12), QNAME, QTYPE/QCLASS (4), and
the existing EDNS OPT record (11). It does not claim an IP/UDP/TCP resolver packet
size or a useful-goodput result.

| Domain | Base32 max payload | Base36 max payload | Base32 query | Base36 query |
|---|---:|---:|---:|---:|
| `t.x.ass-peak.de` | 145 bytes | 147 bytes | 281 bytes | 280 bytes |
| `x.ass-peak.de` | 146 bytes | 148 bytes | 281 bytes | 280 bytes |
| `example.com` | 147 bytes | 150 bytes | 281 bytes | 281 bytes |
| `a.com` | 151 bytes | 154 bytes | 281 bytes | 281 bytes |

For the active domain the maximum raw payload increase is **2/145 = 1.38%**.
For the same 145-byte payload, Base36 reduces the calculated DNS query from 281
to 276 bytes (1.81%); at the maximums, useful payload/query ratio improves about
1.74%. The shorter encoding does not change the downstream Slipstream TXT response,
which carries raw QUIC data rather than a Base32 QNAME.

Representative active-domain values:

| Payload | Base32 encoded/QNAME wire/query | Base36 encoded/QNAME wire/query |
|---:|---:|---:|
| 32 | 52 / 70 / 97 | 51 / 69 / 96 |
| 64 | 103 / 122 / 149 | 101 / 120 / 147 |
| 128 | 205 / 226 / 253 | 202 / 223 / 250 |
| 145 | 232 / 254 / 281 | 228 / 249 / 276 |

## CPU benchmark

The Rust benchmark ran 1,000,000 iterations per operation and size on the same
AMD Ryzen 7 7840HS host. It uses the current Slipstream Base32 implementation and
a direct Rust port of the exact MasterDNS Base36 block algorithm; allocation and
language differences mean this is directional, not a phone power measurement.

| Operation, 145 bytes | Base32 | Base36 |
|---|---:|---:|
| Encode | 291.5 ns/iter | 628.0 ns/iter |
| Decode | 641.5 ns/iter | 256.4 ns/iter |

Base36 encoding is about **2.15× slower** in this comparable Rust microbenchmark;
Base36 decoding is faster because the current Base32 decoder performs more general
validation and supports dotted/padded input. The actual MasterDNS Go benchmark for
Base36 measured 397.6–410.7 ns encode and 328.6–334.1 ns decode for 145 bytes,
with one allocation (240 and 160 bytes respectively). These Go/Rust numbers are
not substituted for Android measurements.

## Robustness findings

- Empty input, all-zero blocks, leading-zero partial blocks, mixed sizes and
  uppercase Base36 decode pass.
- MasterDNS's decoder rejects invalid encoded lengths and characters, but accepts
  syntactically valid-length high-value blocks such as `zzzz` and
  `zzzzzzzzzzz` without overflow error, truncating them to `a0ff` and
  `d39d3e063fffff`. The subsequent VPN frame parser may reject the resulting bytes,
  but this is not a reason to copy the decoder unchanged. Any future implementation
  must add per-block range checks and retain canonical length/case policy.
- Base36 characters remain valid DNS label characters. This changes the alphabet
  from uppercase Base32 to lowercase letters plus all decimal digits; it is an
  encoding/capacity change, **not proven traffic camouflage**.

## Decision

Do **not** build or deploy a compatible Base36 Slipstream client/server pair for
public LTE A/B. The active domain gains only 2 bytes per maximum QNAME (1.38%),
downstream capacity is unchanged, client encoding is materially more expensive,
and the change requires synchronized client/server wire-format migration. The
expected transport improvement is below the evidence threshold and would be
difficult to distinguish from LTE variance without a much larger campaign.

No APK/server runtime was changed. The active state remains the accepted Slipstream
48-worker/64-queue client with explicit DCUBIC. Benchmark helpers and outputs are
ignored under `build/base36/`; the temporary Go benchmark file is inside the ignored
MasterDNS research checkout. Relevant verification commands:

```sh
rustc --edition 2021 -O build/base36/codec_bench.rs -o build/base36/codec_bench
build/base36/codec_bench
GOTOOLCHAIN=go1.26.5 go test ./internal/basecodec -run '^$' \
  -bench='BenchmarkLowerBase36' -benchmem -count=5
```

If later evidence justifies a pair, the next safe design is a versioned dual-codec
server/client with strict decoder overflow checks, explicit fallback only during a
controlled migration, and paired LTE tests. It must not silently change the existing
wire format or claim that Base36 makes the DNS tunnel less detectable.
