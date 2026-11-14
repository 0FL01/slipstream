# Android Network Routing

This page defines Android-side routing terms. Server topology, restricted-network
acceptance boundaries and live routing policy belong to the upper operations
repository.

## TUN implementations

Owenclave offers `gVisor` and `System` TUN implementations. Both use Android's
`VpnService`, but process ownership differs: System TUN can protect sockets owned
through its in-process path, while a separate child process cannot use that
protection path. DNS Tunnel therefore requires gVisor; selecting System TUN with a
DNS Tunnel profile fails before the carrier starts.

## Per-app VPN

Per-app VPN controls which installed application packages enter Android's VPN:

- `Proxy` is an allowlist: selected packages enter the VPN.
- `Bypass` is a denylist: selected packages stay outside the VPN.
- `Off` disables per-app filtering.

Owenclave adjusts its own package membership as required by the selected TUN
implementation. The separate `Allow apps to bypass VPN` setting enables Android's
system-provided `VpnService` bypass mechanism; it is not the per-app package list.
For DNS Tunnel, an empty `Proxy` allowlist fails before Android creates the VPN;
otherwise Android would interpret the missing allowlist as capture all and include
the separate Slipstream child in its own tunnel.
Routing rules may also match packages after traffic has entered the VPN. That
rule-level match does not change which packages Android captures.

## Network type

Routing rules may match the current Android underlay as mobile data, Wi-Fi,
Bluetooth, Ethernet, USB or satellite. Android cellular transport is represented
by the generic value `data`. Owenclave does not distinguish LTE, 4G, 5G, SIM,
carrier, APN, MCC or MNC within that value. The `Metered Network` setting is only
an Android VPN metered hint and is not a carrier selector.

An Android per-app allowlist selects packages for VPN capture. A restricted-LTE
carrier allowlist describes destinations the external mobile network permits.
They are independent acceptance boundaries: selecting an app cannot grant carrier
reachability, and carrier reachability does not select an app for the VPN. No
restricted-LTE carrier allowlist is stored or enforced by this repository.

## Source boundaries

- `app/src/main/java/io/nekohasekai/sagernet/bg/VpnService.kt` applies TUN,
  per-app and Android bypass settings.
- `app/src/main/java/io/nekohasekai/sagernet/database/DataStore.kt` persists the
  user choices.
- `app/src/main/java/io/nekohasekai/sagernet/SagerNet.kt` maps Android transport
  capabilities to the generic network type.
- `app/src/main/java/io/nekohasekai/sagernet/database/RuleEntity.kt` stores
  package and network-type routing matches.
- `app/src/main/java/io/nekohasekai/sagernet/fmt/ConfigBuilder.kt` emits those
  matches into the core routing configuration.

The Android repository owns these client semantics. The upper operations
repository owns restricted-LTE reachability, end-to-end path acceptance and
server routing allowlists; those terms must not be collapsed into the Android
per-app list.
