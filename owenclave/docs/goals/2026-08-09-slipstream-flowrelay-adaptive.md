# Goal: Per-flow FlowRelay DNS tunnel with adaptive idle polling

Status: complete
Source: user-approved FlowRelay plan in the 2026-08-09 DNS reserve session
Last updated: 2026-08-09

Stable contract: [`../dns-tunnel.md`](../dns-tunnel.md). This goal remains the
historical implementation and acceptance record.

## Objective

Replace the shared SSH transport inside the existing Rust Slipstream DNS carrier
with a minimal authenticated FlowRelay, while keeping one independent QUIC stream
per application flow, WARP egress, application TCP and UDP, adaptive idle polling,
one imported resolver, gVisor, certificate pinning, and zero retained traffic
metadata. Production may have bounded downtime but keeps the SSH rollback path until
the complete Android acceptance gate passes.

## Execution Directive

Complete the frozen Required Outcomes using the listed Change Envelope and Primary
Evidence. Work on the smallest unresolved outcome. Do not add requirements from
reviews, tests, tools, speculative risks, or optional source text. Finish when every
required outcome is resolved and affected constraints remain satisfied.

## Frozen Contract

### Required Outcomes

- R1: Remove the abandoned SS2022 candidate without changing production behavior.
  - Source: approved CP0 cleanup.
  - Acceptance: no sing-box/SS2022 service, listener, config, credential copy, or
    temporary P0 artifact remains; production Slipstream still targets private SSH
    `10.200.0.2:41924`; unrelated WARP, microsocks, Jazz, and Jitsi consumers remain
    unchanged.
  - Primary evidence: bounded n-de1 service, target, listener, file, and restart
    inventory before and after cleanup.
  - Status: verified
  - Evidence: n-de1 cleanup removed the candidate binary, config directory, unit,
    enablement link, private `:40001` listener, and transient rollback copy. Before
    and after, production Slipstream PID `1059836` remained active/enabled with zero
    restarts and target `10.200.0.2:41924`; dedicated sshd PID `913120`, WARP units,
    microsocks `:40000`, and all four olcRTC units remained active with zero relevant
    restart growth.

- R2: Prove a private TCP-only FlowRelay P0 before Android or public cutover.
  - Source: approved CP1 stop-gate.
  - Acceptance: pinned `flowd` listens only on private TCP
    `10.200.0.2:40001` inside `warpns`, authenticates the exact FlowRelay/1 OPEN,
    transfers an exact 4096-byte payload through WARP, rejects a wrong token before
    resolution/connect, and emits no retained output; production remains on SSH.
  - Primary evidence: Go tests/build digest and private end-to-end byte/hash,
    wrong-token, WARP-egress, listener, output, PID, and restart observations.
  - Status: verified
  - Evidence: Go unit/race tests and vet passed; the static linux/amd64 build digest
    is `e462cb9954f073bbb1f3e3fe3f530a53a0010d85c45ad639eb3fc26fe0d5e153`.
    `flowd.service` passed `systemd-analyze verify`, is active/enabled with PID
    `1107156`, zero restarts, and null stdout/stderr, and listens only on private TCP
    `10.200.0.2:40001`. A domain OPEN returned an exact 4096-byte static object with
    SHA-256 `e3073c92caca774575a31c468d6c0697ea32717cfe1df14f3e7f79b1aef5fceb`;
    Cloudflare trace reported `warp=on`; a wrong token was reset and the focused test
    proved it never calls the dialer. Production stayed on SSH with unchanged PID and
    zero restarts.

- R3: Integrate FlowRelay TCP and preserve adaptive Slipstream behavior.
  - Source: approved CP2 client design and adaptive battery requirement.
  - Acceptance: the existing DNS Tunnel `SSHBean` persists one 16-byte Flow token;
    `dnstt://` imports/exports it as 32 lowercase hex; Exclave generates one
    authenticated SOCKS5 outbound to the loopback Slipstream listener with no mux or
    UoT; the opt-in Rust client accepts only loopback SOCKS5/RFC1929 CONNECT, queues
    the compact OPEN before payload, and receives its fixed bootstrap through stdin
    on every start. At zero local streams authoritative poll bursts are suppressed
    and idle keepalive is 5000 ms; the first stream immediately restores 400 ms and
    normal polling without reconnecting.
  - Primary evidence: focused parser/config/persistence tests, Rust protocol and
    adaptive tests, pinned arm64 build digest, and generated command/config inspection.
  - Status: verified
  - Evidence: the release Kotlin and Java compile tasks passed under JDK 21. A
    focused JVM check proved that `SSHBean` serialization and `clone()` preserve
    a 32-hex token with `AUTH_TYPE_PASSWORD`. Source/config inspection proved one
    SOCKS5 user, loopback-only target, no mux/smux, no token in Exclave JSON, and
    `--flow-relay-stdin` as the only new argv field. Rust tests passed the exact
    80-byte bootstrap, fragmented RFC1929/domain CONNECT, IPv4/IPv6 OPEN vectors,
    wrong-password rejection, and adaptive 400/5000 ms decisions. The pinned
    Android 21 AArch64 NDK r29 build passed with SHA-256
    `6be785425bbdd9401f2e7005e50cd2f302d17420907084fe911a8ea8b62d9914`.

- R4: Add and prove application UDP without changing the server wire to sing-UoT.
  - Source: approved CP4 UDP stop-gate.
  - Acceptance: the pinned Exclave sing-UoT v2 boundary is translated inside the
    Android Rust adapter to FlowRelay-native UDP records; one application UDP source
    association maps to one local TCP connection, one QUIC stream, and one private
    `flowd` UDP association; an exact application UDP nonce passes through WARP while
    TCP remains independently cancellable and unauthorized UDP fails.
  - Primary evidence: exact pinned-format unit/interoperability tests and private
    end-to-end TCP, UDP nonce, wrong-token, isolation, WARP, listener, and restart
    observations.
  - Status: verified
  - Evidence: the exact pinned UoT v2 bind vector passed in the Rust adapter: it
    consumed only the magic CONNECT and initial bind destination, emitted command-1
    FlowRelay OPEN, and left the first custom address/length/payload record byte-for-byte
    unchanged. All 22 Rust client tests passed, and the Android 21 AArch64 NDK r29
    artifact has SHA-256
    `ee14b7227eb219eb089bf946420105af08eb61c88bbfadd2e84e44c36859d838`.
    Go race tests and vet passed for the FlowRelay-native UDP record loop; the deployed
    static backend digest is
    `eb07424fe9cee1102c136ec6ef6884388e93806174816ce5d3b3906bcd511a6e`.
    A private UDP association preserved an exact randomized DNS nonce and Cloudflare
    `whoami` matched the independently observed `warp=on` egress. Unauthorized UDP was
    rejected before opening a UDP socket; the 4096-byte TCP regression retained SHA-256
    `e3073c92caca774575a31c468d6c0697ea32717cfe1df14f3e7f79b1aef5fceb`.
    `flowd` PID `1115762` stayed active with zero restarts and only private TCP
    `10.200.0.2:40001`; the release Kotlin/Java compile passed with UoT enabled only on
    this SOCKS outbound.

- R5: Perform a bounded serial production cutover with demonstrated SSH rollback.
  - Source: approved CP5 and permission for non-critical downtime.
  - Acceptance: a fresh timestamped backup of `/etc/systemd/system/slipstream.service`
    exists; only its private fixed target changes from SSH to FlowRelay; public UDP
    `:53`, domain, TLS material, certificate pin, hardening, limits, namespace, and
    null output policy remain; restoring the SSH target is demonstrated.
  - Primary evidence: FlowRelay/unit validation, exact unit diff, listeners,
    active/enabled state, bounded restart counts, and rollback proof.
  - Status: verified
  - Evidence: fresh backup
    `/etc/systemd/system/slipstream.service.bak.20260809T161130Z` preserves the SSH
    target. The validated unit diff changed only `--target-address` from private
    `:41924` to private `:40001`; public UDP `195.128.101.186:53` and every other
    ExecStart/unit property stayed unchanged. The exact pinned Linux client passed a
    4096-byte TCP hash and UoT-to-FlowRelay randomized UDP nonce through the public
    Slipstream listener. Restoring the backup byte-for-byte and restarting only
    Slipstream returned the live target to `:41924`; the unit was then validated and
    cut back to `:40001`. Final Slipstream PID `1118933`, `flowd` PID `1115762`, and
    retained DNS sshd PID `913120` were active with zero restarts.

- R6: Pass full Android acceptance, then retire the dedicated DNS SSH path.
  - Source: approved absolute acceptance and retirement gates.
  - Acceptance: on the installed arm64 APK using one imported TCP-resolver gVisor
    profile, exact 4096-byte TCP completes within 10 seconds, exact 8 MiB TCP within
    75 seconds, and a real application UDP nonce within 10 seconds; both endpoints
    observe WARP; three cycles of eight downloads cancelled after 15 seconds are each
    followed by unrelated exact TCP and UDP probes within 10 seconds; after ten
    screen-off idle minutes an exact 4096-byte probe completes within 10 seconds;
    relevant PIDs/restart counters remain stable. Only then remove the dedicated DNS
    sshd runtime/config/authorization and obsolete Android SSH behavior while keeping
    administration SSH and shared microsocks `10.200.0.2:40000`.
  - Primary evidence: installed APK path/digest, Android runtime hashes/nonces/timing,
    egress observations, PID/restart state, final remote inventory, and focused
    repository checks.
  - Status: verified
  - Evidence: `:app:assembleOssRelease` passed under JDK 21. The installed arm64 APK
    has SHA-256 `3f6b7a932d324c7433d859dc93361ba90009aa54558a4a381c6231ab1b72b13d`
    and uses the user-approved replacement release signer. One TCP-resolver profile
    connected through gVisor. Exact 4096 bytes passed in 268 ms and exact 8 MiB in
    58.784 s with the expected hashes; randomized application UDP passed in 149 ms.
    Cloudflare reported `warp=on` for TCP and a WARP egress address for UDP. Three
    cycles each opened and cancelled eight downloads after 15 seconds; their following
    exact TCP probes completed in 254/395/304 ms and UDP in 148/171/167 ms. After 600
    seconds screen-off in Doze, exact 4096 bytes completed in 250 ms. Android app,
    service, and Slipstream PIDs stayed `20640`, `20453`, and `21347`; server Slipstream
    and `flowd` had zero restart growth throughout acceptance. Afterward the dedicated
    sshd, private `:41924`, authorization/account, and legacy dnstt runtime/account were
    removed. Slipstream now requires `flowd.service`; final Android TCP, UDP, and WARP
    probes passed, both services are enabled/active with zero restarts and null output,
    and shared microsocks plus all olcRTC units remained active.

### Constraints

- C1: Exactly one profile-supplied `udp://` or `tcp://` resolver and one Slipstream
  process/path; no resolver discovery, fallback, rotation, ranking, or multipath.
- C2: DNS Tunnel remains one `SSHBean` profile model and gVisor-only; no Room entity,
  second VPN implementation, Exclave-core fork, protobuf change, or AAR rebuild.
- C3: One app TCP flow or UDP association maps to one local TCP connection and one
  independent Slipstream QUIC stream. No mux, smux, native SOCKS UDP ASSOCIATE,
  XUDP, or QUIC DATAGRAM fragmentation.
- C4: FlowRelay/1 TCP OPEN is `[meta:1][token:16][destination][port:u16be]` followed
  by raw bytes. It has no HELLO, positive remote ACK, inner crypto, padding, 0-RTT
  OPEN, fallback, or userspace preconnect queue.
- C4a: FlowRelay/1 UDP OPEN uses the same header with command `1`. Each following
  record is `[atype:1][destination][port:u16be][length:u16be][payload]`, where
  address types are `0` IPv4, `1` IPv6, and `2` domain. The Android adapter alone
  consumes the pinned sing-UoT v2 bind header; its subsequent records are
  byte-identical to this custom framing and are forwarded without re-encoding.
- C5: The local SOCKS boundary requires ephemeral RFC1929 credentials, has a bounded
  five-second handshake, and receives token and credentials only through stdin, not
  argv, environment, or temporary files.
- C6: `flowd` is private inside `warpns`, binds outbound DNS and destination sockets
  fail-closed to `CloudflareWARP`, uses one fixed WARP-bound resolver, and exposes no
  public proxy, controller, API, recursion, cache, or direct-host fallback.
- C7: Keep Slipstream leaf-certificate pinning and never print or commit tokens,
  credentials, complete connection URIs, private keys, keystores, or databases.
- C8: Preserve zero-retained-traffic-metadata behavior: service stdout/stderr null,
  no application or packet logs, and no persistent carrier output.
- C9: Do not reconfigure shared WARP, microsocks `:40000`, Xray, Jazz, Jitsi,
  nftables, or administration SSH unless a proven blocker expands this envelope.

### Non-goals

- A separate Android FlowClient process, direct FlowRelay Exclave outbound, public
  Slipstream protocol changes, host-wide WARP, raw IP tunnel, or SSH/SS compatibility
  in the final FlowRelay wire/backend.
- Resolver tuning, MTU/FEC/QUIC DATAGRAM work, connection-level authorization,
  scheduler redesign, compression, or throughput claims beyond the acceptance gates.

## Change Envelope

- Target: existing DNS Tunnel profile/runtime, pinned Rust Slipstream Android client,
  one standalone Go stdlib `flowd`, and the production Slipstream fixed target.
- Expected local paths:
  - `app/src/main/java/io/nekohasekai/sagernet/fmt/ssh/DNSTTFmt.kt`
  - `app/src/main/java/io/nekohasekai/sagernet/fmt/ConfigBuilder.kt`
  - `app/src/main/java/io/nekohasekai/sagernet/database/ProxyEntity.kt`
  - `app/src/main/java/io/nekohasekai/sagernet/ui/compose/ComposeActivities.kt`
  - `app/src/main/java/io/nekohasekai/sagernet/ui/compose/screens/UniversalProfileSettingsScreen.kt`
  - `app/src/main/java/io/nekohasekai/sagernet/bg/proto/SlipstreamInstance.kt`
  - `bin/lib/slipstream/` pinned patch/build inputs and `flowd/`
  - `AGENTS.md`, this goal, and generated ignored arm64 artifacts
- Expected remote paths:
  - `/usr/local/bin/flowd`
  - `/etc/flowd/token`
  - `/etc/systemd/system/flowd.service`
  - `/etc/systemd/system/slipstream.service` and one timestamped backup
  - retired candidate `/usr/local/bin/sing-box-dns-reserve`,
    `/etc/sing-box-dns-reserve/`, and
    `/etc/systemd/system/sing-box-dns-reserve.service`
- Allowed artifacts: one Go stdlib binary/service/credential, pinned local source and
  patches, ignored arm64 executable/APK, and bounded temporary exact-payload test
  endpoints removed after use.
- Forbidden artifacts: secrets or complete links in Git/tool output, public relay
  listeners, persistent traffic logs, fallback paths, duplicate carrier processes,
  schema migration, Exclave fork, or shared-consumer changes.
- Budget: first sufficient implementation; stop at failed TCP P0, failed UDP P0, an
  irreproducible pinned Android build, or a proven inability to bind WARP fail-closed.

## Current Checkpoint

- Closes: none; all required outcomes are verified.
- Smallest next action: none.
- Expected evidence: closure evidence is recorded below.
- Stop or replan if: not applicable.

## Current State

- Resolved: R1-R6; production and the installed Android client use per-flow FlowRelay
  TCP/UDP through adaptive Slipstream and WARP.
- Last relevant evidence: final Android TCP/UDP/WARP probes passed after SSH retirement;
  public UDP `:53` and private FlowRelay `:40001` are active with zero restarts, and
  private SSH `:41924` is absent.
- Blocker: none.
- Next: none; the objective is complete.

## Material Decisions

- 2026-08-09: replace SS2022/sing-box with minimal FlowRelay; retain no SS wire or
  backend compatibility.
- 2026-08-09: integrate the loopback SOCKS/RFC1929 adapter into the existing Rust
  Slipstream client; do not add a process or fork Exclave core.
- 2026-08-09: persist the 16-byte Flow token as 32 lowercase hex in
  `SSHBean.password` with `AUTH_TYPE_PASSWORD`; generate local SOCKS credentials per
  built service configuration.
- 2026-08-09: use standalone Go stdlib `flowd` inside `warpns`; public Slipstream
  remains a fixed-target carrier and is not modified.
- 2026-08-09: TCP P0 precedes UDP, but production cannot remain cut over until the
  application-UDP stop-gate passes. UDP translation is pinned sing-UoT v2 only at the
  Android boundary; FlowRelay wire and `flowd` remain custom and minimal.
- 2026-08-09: downtime during serial production work is acceptable; rollback and all
  acceptance evidence remain mandatory.
- 2026-08-09: pinned Exclave uses one bind-mode UoT v2 stream per source association.
  Consume only its initial bind/destination header on Android; define FlowRelay UDP
  records with the same compact address/length bytes so neither `flowd` nor the wire
  contains UoT semantics.
- 2026-08-09: the user authorized replacing the unavailable Jazz release signer and
  removing an incompatible installed release; release app data preservation is no
  longer a cutover constraint.

## Checkpoint History

- 2026-08-09: abandoned SS2022 objective after its private P0; production was never
  cut over and no APK was installed.
- 2026-08-09: RECON froze FlowRelay boundaries, wire, ownership, TCP/UDP staging, and
  acceptance plan; user approved iterative execution.
- 2026-08-09: R1 passed. The abandoned sing-box candidate and transient backup were
  removed; production SSH target and unrelated services remained unchanged.
- 2026-08-09: R2 passed. The standalone private `flowd` build, unit, exact 4096-byte
  relay, wrong-token rejection, WARP egress, stable runtime, and null output passed;
  production remained on SSH.
- 2026-08-09: R3 passed. Token persistence/clone, SOCKS config and stdin boundaries,
  Rust protocol/adaptive tests, JDK 21 release compilation, patch replay, and the
  pinned Android 21 AArch64 build passed; production remained on SSH.
- 2026-08-09: R4 passed. Exact pinned UoT-to-FlowRelay translation, Go UDP framing,
  arm64 rebuild, private randomized nonce, WARP egress, unauthorized rejection, TCP
  regression, listener and zero-restart gates passed; production remained on SSH.
- 2026-08-09: R5 passed. A one-line fixed-target cutover, exact pinned TCP/UDP client
  proof, byte-for-byte SSH rollback, and final FlowRelay recutover passed with stable
  services. R6 initially stopped at unavailable release signing credentials.
- 2026-08-09: R6 passed after the user authorized a replacement release signer. The
  signed arm64 APK, exact TCP/UDP/WARP, cancellation, screen-off idle, PID/restart,
  retirement, final remote inventory, documentation, and repository checks passed.

## Completion

- Resolved outcomes: R1-R6 verified.
- Commands and artifacts: focused Go race/vet, Rust protocol/adaptive tests, pinned
  Android NDK build, JDK 21 release compile/assemble, signed APK install, Android exact
  TCP/UDP/WARP/cancellation/idle probes, `systemd-analyze verify`, nftables check,
  listener/service/account inventory, `git diff --check`, and focused secret scans.
- Constraint and diff-scope check: one imported resolver, gVisor, one FlowRelay path,
  per-flow QUIC streams, WARP-only egress, certificate pinning, null service output,
  no public proxy/fallback/schema/core fork, and no shared-service changes remain true.
- Final status: complete.
