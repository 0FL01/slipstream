# Owenclave

Android proxy client: application ID `org.owenewans.owenclave`, Kotlin namespace `io.nekohasekai.sagernet`.

## Map and contracts
- `docs/dns-tunnel.md`: DNS Tunnel profile, token/pin, sequential automatic selection, explicit multipath and bounded foreground benchmark contracts.
- `docs/android-network-routing.md`: gVisor TUN, per-app and network-type capture; capture is not a stealth guarantee.
- `app/src/main/java/io/nekohasekai/sagernet/fmt/dnstt/`: dedicated DNS profile/provisioning; generic SSH/dnstt provisioning is absent.
- `app/src/main/java/io/nekohasekai/sagernet/bg/proto/SlipstreamInstance.kt`: one-child carrier lifecycle and bounded TCP DNS adapters.
- `bin/lib/slipstream/build.sh`: non-destructive sibling Rust arm64 build; patches are already applied.
- `bin/lib/slipstream/flowd/`: sole private FlowRelay backend and tests.
- `library/core/`: pinned carrier-only wrapper and retained-config parser.

## Rules
- Pass Flow token and ephemeral local SOCKS credentials only through child stdin. One app TCP flow owns one independent QUIC stream; no mux/smux, fallback or probe streams.
- DNS Tunnel is gVisor-only. Automatic selection retains one child/resolver; explicit multipath must stay explicit, with aggregate worker/queue budgets. Never silently expand a single manual override.
- Preserve paired multipath bootstrap/server path-identity patches, activity-aware polling and bounded readiness; consult DNS contract before edits.
- Keep all foreground latency/benchmark child launch/cancel/cleanup serialized; failures retain the previous saved benchmark snapshot.
- Keep generated jniLibs and signing credentials out of Git. External signing environment is documented in root README.
- Active multipath-throughput and blocked jitsi-recovery goals remain independent unfinished work.

## Build and verify
Use JDK21, SDK37/Build-Tools37, NDK29, Go1.26+ (`nix develop` where available).
```sh
./run lib core
./bin/lib/slipstream/build.sh
./gradlew :app:downloadAssets
./gradlew :app:compileOssReleaseKotlin
./gradlew :app:lintOssRelease
./gradlew test
(cd bin/lib/slipstream/flowd && go test ./...)
```
Device qualification is separate: automatic/manual setup, one live child, exact gVisor payload and expected egress. Do not claim it from a build.
