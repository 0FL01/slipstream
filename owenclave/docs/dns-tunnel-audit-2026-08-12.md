# DNS Tunnel audit findings — 2026-08-12

Status: RECON complete; reproduction pending

This document records source-audit findings, not confirmed runtime defects. Reproduce
each reachable case before changing code. Do not use profile credentials, process
arguments or traffic logging as evidence.

## Wave 1 — direct device reproduction

### W1.1 Direct route mode can bypass the carrier

- Severity: critical
- Status: reproduced and fixed in R1
- Confidence: confirmed on Android 15
- Source: `fmt/ConfigBuilder.kt:811-817`
- Trigger: start a DNS Tunnel profile while global Route Mode is `Direct`.
- Risk: the catch-all rule selects the direct outbound, so application TCP/UDP and
  benchmark HTTP traffic may use the physical underlay instead of DNS Tunnel.
- Reproduce: compare a controlled destination's egress and payload with Route Mode
  `Global` and `Direct`; also verify whether payload survives loss of the carrier.
- Evidence: before the fix, Route Mode `Direct` produced direct LTE egress
  `loc=RU`, `warp=off` while VPN and one Slipstream child remained active. After
  excluding DNS Tunnel from the direct catch-all, the same mode and payload produced
  DE WARP egress with one child; TCP and UDP probes passed.

### W1.2 Test all can overlap the live DNS Tunnel

- Severity: high
- Confidence: high from source
- Source: `ui/compose/screens/ConfigurationScreen.kt:165-206,388-401`,
  `bg/test/V2RayTestInstance.kt:54-87`, `bg/proto/SlipstreamInstance.kt:274-335`
- Trigger: press Test all while a DNS Tunnel VPN is connected.
- Risk: the main and service processes can own separate Slipstream children and the
  same certificate file. Test cleanup can remove the certificate needed by a later
  production-child restart.
- Reproduce: observe child count during Test all, then verify live payload and a
  controlled VPN restart. Do not print child command lines.
- Evidence: Android 15 reproduced two concurrent Slipstream children immediately
  after Test all was pressed beside a connected DNS Tunnel VPN. The fixed UI disables
  Test all while the service is starting or connected and rechecks the state in the
  action handler; the active VPN retained one child and its payload/restart stayed
  healthy.

### W1.3 Benchmark Cancel exposes cleanup as complete too early

- Severity: high
- Confidence: high for unordered cleanup
- Source: `ui/compose/screens/ConfigurationScreen.kt:216-224`,
  `bg/test/V2RayTestInstance.kt:93-103`
- Trigger: cancel an active benchmark and immediately start the VPN.
- Risk: the dialog disappears before benchmark teardown has joined. Test cleanup can
  overlap production startup and interfere with its child or shared certificate.
- Reproduce: repeat Cancel -> immediate Connect while recording child count, service
  state, selected resolver and controlled payload outcome.
- Evidence: the prior action removed the dialog immediately after cancellation even
  though cleanup completion was not part of that UI transition. The fixed action keeps
  the dialog in its running state until `cancelAndJoin` completes; only then does it
  expose Connect, with zero benchmark children.

### W1.4 Rapid resolver choices are not serialized

- Severity: medium
- Confidence: high from source
- Source: `ui/compose/screens/ConfigurationScreen.kt:226-242`,
  `ui/compose/components/DnsttBenchmarkDialog.kt:64-107`
- Trigger: while the benchmark continues, tap a completed result and immediately tap
  another result or Automatic TCP.
- Risk: independent save coroutines can finish out of order, so the earlier tap can
  overwrite the user's final choice.
- Reproduce: alternate two visibly distinct choices and reopen the profile; compare
  the displayed final selection with the last tap.
- Evidence: Android reproduced resolver-first/Automatic-second ending with the first
  resolver persisted even though both controls accepted taps. The fix accepts exactly
  one choice and immediately disables every resolver plus Automatic until cleanup and
  persistence finish, eliminating concurrent selection owners.

### W1.5 Empty per-app Proxy list captures every app

- Severity: high
- Confidence: high from source and Android VPN semantics
- Source: `bg/VpnService.kt:249-272`, `docs/android-network-routing.md:15-27`
- Trigger: enable per-app `Proxy`, leave its application list empty, then start DNS
  Tunnel with gVisor.
- Risk: Android receives no allowed-app entries, which means all applications are
  captured. Owenclave and its child may also enter the VPN, creating carrier
  recursion and readiness failure instead of capturing no applications.
- Reproduce: compare two ordinary applications with an empty list and a one-app list;
  record capture and carrier readiness separately.
- Evidence: Android 15 reproduced an empty `Proxy` list creating a VPN and one
  Slipstream child before carrier recursion stopped both after the readiness window.
  The focused fix rejects that state before retaining either resource. With Firefox
  as the only selected app, one child remained connected, Firefox reached DE WARP,
  and unselected Termux received no external payload on restricted LTE.

## Wave 2 — network and mode lifecycle

### W2.1 Restart can lose its replacement network listener

- Severity: medium
- Confidence: medium-high race
- Source: `bg/VpnService.kt:125-139,168-183`,
  `utils/DefaultNetworkListener.kt:61-77`
- Trigger: consecutive physical underlay changes.
- Risk: asynchronous teardown from the first restart can unregister the listener
  installed by the replacement session. The next handover is then ignored.
- Reproduce: perform at least two consecutive Wi-Fi/LTE handovers and require a child
  restart plus payload recovery after each one.
- Evidence: Android 15 reproduced Wi-Fi to LTE replacing the child, while the next
  LTE to Wi-Fi transition stopped the service instead of recovering. Listener sessions
  now use unique keys, and teardown resets the old underlay baseline. With one strict
  resolver to isolate listener ownership from automatic candidate timing, both fixed
  transitions replaced the child, retained one VPN/child and reached DE WARP.

### W2.2 Proxy service mode violates the gVisor-only boundary

- Severity: high contract mismatch
- Confidence: high from source
- Source: `bg/proto/V2RayInstance.kt:142-151`, `bg/ProxyService.kt:47-67`
- Trigger: select Proxy service mode and start a DNS Tunnel profile.
- Risk: no Android VPN/gVisor captures ordinary application traffic, and underlay
  changes do not restart the selected DNS resolver.
- Reproduce: confirm absence of VPN capture, application egress and child behavior
  across a Wi-Fi/LTE handover.
- Evidence: Android 15 reproduced Proxy service reporting active with one Slipstream
  child but no Android VPN; Firefox bypassed the carrier with `loc=RU`, `warp=off`.
  The focused runtime guard now rejects this mode before child launch. Returning to
  VPN/gVisor retained one child, passed TCP/UDP and restored DE WARP egress.

### W2.3 Automatic mode misses same-network DNS changes

- Severity: medium
- Confidence: medium-high from source
- Source: `utils/DefaultNetworkListener.kt:113-143`,
  `bg/proto/V2RayInstance.kt:236-275`
- Trigger: DHCP or link-properties DNS changes while Android retains the same Network.
- Risk: automatic mode keeps an obsolete network-local resolver and does not recover.
- Reproduce: on controlled Wi-Fi, change advertised DNS without disconnecting and
  verify Network identity, child PID and payload recovery.
- Status: closed without a fix by user direction. Bounded Android 15 attempts did not
  produce a same-Network DNS-list change, and speculative callback changes were removed;
  no router or server setting was altered.

## Wave 3 — controlled fault injection

### W3.1 Replacement child is not readiness-gated

- Severity: high
- Confidence: high from source
- Source: `bg/proto/SlipstreamInstance.kt:188-203,297-323`
- Trigger: the ready child exits, then its resolver is unavailable during restart.
- Risk: a live but unready replacement can leave the service reporting Connected
  indefinitely while payload is blocked.
- Reproduce: use a disposable resolver or controlled forwarder; never disrupt the live
  public carrier for this test.
- Evidence: Android 15 connected through a disposable localhost TCP DNS forwarder.
  After the ready child was killed and the forwarder removed, the baseline immediately
  launched a live replacement and remained Connected indefinitely. The fix routes the
  exit through full service startup: the replacement stayed under the 15-second
  Connecting gate and the VPN/child stopped when it could not become ready.

### W3.2 Failed gVisor TCP dials retain connection state

- Severity: medium
- Status: fixed and verified
- Confidence: confirmed
- Source: `library/core/tun.go:287-316`
- Trigger: repeated captured TCP attempts while the carrier SOCKS dial fails.
- Risk: the error return skips context cancellation, list removal and incoming
  connection close, accumulating resources until VPN shutdown.
- Evidence: a focused deterministic core test forced `V2RayInstance.dial` to fail.
  The baseline retained one connection-list entry and left the incoming `net.Conn`
  open; the fixed path cancels the context, removes the entry and closes the incoming
  connection on every return. The rebuilt arm64 core was packaged in a signed release;
  Android 15 retained one VPN/child and Firefox reached DE WARP through gVisor.

### W3.3 Stop and restart requests can cross

- Severity: medium
- Status: fixed and verified
- Confidence: confirmed
- Source: `bg/BaseService.kt:401-445`
- Trigger: a user stop or fatal callback arrives while an underlay/reload restart is
  already in Stopping.
- Risk: the stop request can be ignored and the VPN starts again unexpectedly.
- Evidence: a temporary five-second teardown barrier made the ordering deterministic.
  On the baseline, `RELOAD` followed by `CLOSE` replaced child PID 14363 with 14854 and
  retained the VPN. The fixed build records final stop intent during `Stopping`; the
  identical sequence removed child PID 15231 and the VPN, with neither returning during
  the bounded observation. The barrier and debuggable release flag were removed.

## Additional findings

- Batch tests and profile editing can overwrite each other's whole Room row:
  `ConfigurationScreen.kt:165-194`, `database/ProfileManager.kt:116-119`.
- Leaving Configuration during Test all can retain phantom progress and block DNS
  benchmark: `ConfigurationScreen.kt:165-213`.
- A child bootstrap failure can be insufficiently joined on API 23-25:
  `bg/proto/SlipstreamInstance.kt:215-261`.
- Automatic candidates split the 15-second aggregate deadline into shorter shares:
  `bg/proto/V2RayInstance.kt:240-249`.
- TCP DNS adapter overload drops queued packets without an explicit failure signal:
  `bg/proto/SlipstreamInstance.kt:70-99`.

## Evidence contract

For every reproduction record the trigger, Android network and VPN state, service
state, child count, selected mode/resolver label, TCP and UDP payload result, final
egress, cleanup state and unchanged `n-de1` restart counters. A source finding becomes
a confirmed defect only after its observable contract violation is reproduced.
