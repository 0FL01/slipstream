# Goal: Jitsi recovery without forced smartphone wakefulness

Status: blocked
Source: user request to implement the audited recovery plan, build APK and copy to /home/stfu/Torrents/apk/
Last updated: 2026-09-26

## Objective
Deliver an Android APK with autonomous Jitsi recovery after temporary connectivity loss, without enabling permanent wake locks or adding frequent background probes.

## Execution Directive
Complete the frozen Required Outcomes using the listed Change Envelope and Primary Evidence. Work on the smallest unresolved outcome. Do not add requirements from reviews, tests, tools, speculative risks, or optional source text. Finish when every required outcome is resolved and affected constraints remain satisfied.

## Frozen Contract
- R1: Preserve and implement the audited recovery plan.
  - Source: user request following Jitsi lifecycle and battery audit.
  - Acceptance: temporary carrier/handshake failures do not permanently strand the live client; Android handles meaningful underlay loss/return with serialized, cancellable recovery.
  - Primary evidence: focused Go regression tests and Android compilation; device payload acceptance when a device is available.
  - Status: blocked
  - Evidence: source implementation and regression tests complete; device payload/sleep acceptance remains unavailable (adb devices lists no devices).
- R2: Preserve smartphone power defaults.
  - Source: user requires smartphone usability without undesirable battery drain.
  - Acceptance: no automatic WakeLock/WifiLock/battery-exemption, no new periodic probes; retry has bounded pacing and cancellation.
  - Primary evidence: diff inspection and recovery tests. Actual idle battery cost requires a phone measurement.
  - Status: verified
  - Evidence: power defaults unchanged; no WifiLock, automatic battery exemption or periodic probes added. Android waits offline and uses 5/10/20/40/60-second failure pacing. Actual idle battery cost remains unmeasured.
- R3: Build and deliver APK.
  - Source: copy APK to /home/stfu/Torrents/apk/.
  - Acceptance: arm64 APK built with modified carrier and copied to destination, hashes match.
  - Primary evidence: build command, APK inspection and sha256sum.
  - Status: verified
  - Evidence: assembleOssRelease succeeded; apksigner verifies v1/v2 signatures; packaged arm64 libolcrtc.so matches rebuilt native library. Delivered /home/stfu/Torrents/apk/Owenclave-0.17.50-jitsi-recovery-arm64-v8a.apk (34894656 bytes), SHA256 e762688a05403d2ee9dd828020ec0562f44b02cf2981ef9e3ef57315b08722e5, identical to build output.

## Constraints and Non-goals
- Preserve routing, user Stop, profile replacement, authentication and one-child ownership.
- No server changes, secrets, persistent traffic telemetry, new dependencies or generic retry framework.
- No deviceID persistence, kiosk UI, watchdog, unconditional ICE Disconnected teardown or forced night-time wakefulness.
- Do not remove the one-second RTP heartbeat without JVB runtime evidence: existing code documents endpoint expiration without it.
- 99.9% availability and low battery consumption are not proven by compilation. Powered-off/no-network intervals cannot provide service.

## Change Envelope
- Android: existing network listener, VPN/service lifecycle and native carrier startup; closest tests.
- Go: client session recovery and Jitsi reconnect lifecycle; closest tests.
- Packaging: olcrtc build script/local source support, bundled arm64 library and APK.
- One client-owned recovery execution path is permitted to keep handshake retries from blocking the Jitsi supervisor; no independent watchdog.

## Copied audited plan
1. Repair Go handshake recovery ownership before extending retry lifetime; keep bounded attempts/backoff/cancellation and reject stale session publication.
2. Repair Android meaningful network loss/return recovery using existing lifecycle; retain recovery intent through temporary failures and cancel on Stop/profile change.
3. Preserve battery defaults and wait for available physical network rather than spinning process restarts offline.
4. Build carrier and APK, then verify artifact. Device acceptance: prolonged offline/online, carrier failure without network change, sleep/reboot and Stop during recovery, checking useful payload rather than Connected.
5. Measure idle battery on Wi-Fi/LTE before claiming efficiency or adjusting RTP heartbeat.

## Current Checkpoint
- Closes: R1
- Next: install delivered APK on an authorized target device and verify useful payload after prolonged offline, sleep/reboot and Stop during recovery; measure idle battery on Wi-Fi/LTE.
- Stop or replan if: no target device is available. Do not claim runtime uptime or battery measurements from a successful build.

## Current State
- Resolved: audited plan recorded, Go recovery and Android VPN recovery implemented, APK delivered.
- Android: actual initialized Jitsi graph detection; existing connectingJob owns offline waiting/restarts, fresh DNS configuration and joined child cleanup. Proxy-only recovery was not extended.
- Go: one cancellable client recovery worker, continued handshake retry without blocking Jitsi supervisor, stale-publication rejection and late-peer recovery callback. Jitsi engine cap and RTP heartbeat remain unchanged.
- Packaging: pinned upstream commit remains 35881bc206abd56fea107e72d63f186bf08aac1c with checked-in bin/lib/olcrtc/jitsi-recovery.patch applied by build.sh, including new source/tests. No commits made.
- Last relevant evidence: go test -race -count=1 -timeout 90s ./internal/client ./internal/engine/jitsi passed; five focused Android JUnit tests passed via cached Kotlin/JUnit runner; compileOssReleaseKotlin and assembleOssRelease passed; git diff --check passed in both repos.
- Native build: ANDROID_NDK_HOME=/home/stfu/.cache/android-sdk/ndk/29.0.14206865 OLCRTC_SRC=/home/stfu/.cache/opencode-tmp/opencode/olcrtc-jitsi-recovery-verify ./bin/lib/olcrtc/build.sh succeeded for all four ABIs.
- APK build: ANDROID_HOME=/home/stfu/.cache/android-sdk ANDROID_SDK_ROOT=/home/stfu/.cache/android-sdk ./gradlew :app:assembleOssRelease succeeded using JDK 21.
- Blocker: /home/stfu/.cache/android-sdk/platform-tools/adb devices returned an empty device list. No approved device runtime evidence is available. Smallest unlock: authorized phone/head-unit connected via ADB or user testing the delivered APK.
- Known limits: existing native stream operations can delay cancellation until their timeouts; actual battery cost and OEM process/power behavior are unverified. A live native carrier can still spend energy on the existing one-second RTP heartbeat.

## Checkpoint History
- 2026-09-26: implemented source recovery, focused tests passed, reproducible patched native build and signed release APK delivered. Local artifact work finished; device acceptance is blocked by missing hardware.

## Completion
- R2/R3 verified; R1 local implementation verified, runtime acceptance blocked. Overall status blocked, not complete. No claim of 99.9% uptime, <=30-second recovery or measured low battery drain.
