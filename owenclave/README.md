<div align="center">

```
                          __                    
 ___ _    _____ ___  ____/ /_____
/ _ \ |/|/ / -_) _ \/ __/ __/ __/
\___/__,__/\__/_//_/_/  \__/\__/ 
```

fork of exclave (best proxy client in the world) with custom features
<br>

</div>

stack       kotlin / java / go / android sdk / gradle / nix
license     gpl-3.0-or-later

features:
- DNS Tunnel over Slipstream/FlowRelay
- olcRTC with Jitsi, Jazz, Telemost/Goolom and WBStream/LiveKit
- group and routing support
- twps2 (zapret2) global dpi bypass
- unlock ai and en services for russia (global)
- direct proxy mode
- material 3 expressive ui

profile families:
- DNS Tunnel (gVisor only)
- olcRTC

olcrtc and Slipstream run as external native sidecars. Owenclave owns their
Android profile, configuration and lifecycle boundaries; pinned upstream sources
are built by `bin/lib/olcrtc/build.sh` and `bin/lib/slipstream/build.sh`. See
[`docs/dns-tunnel.md`](docs/dns-tunnel.md) and
[`docs/android-network-routing.md`](docs/android-network-routing.md).

quick start:
```sh
git clone https://github.com/owenewans/owenclave --recurse-submodules
cd owenclave
# install jdk 21, go 1.26, go mobile, android sdk
./run lib core
./bin/lib/slipstream/build.sh
./gradlew :app:assembleOssRelease
```

nix users:
```sh
git clone https://github.com/owenewans/owenclave --recurse-submodules
cd owenclave
nix develop
./run lib core
./bin/lib/slipstream/build.sh
./gradlew :app:assembleOssRelease
```

build from source:
- install and configure jdk 21, go 1.26 and go mobile
- android sdk is provided via nix flake or install manually:
  - android sdk platform 37.0, android sdk build-tools 37.0.0, android sdk platform-tools and android ndk r29
- signing is external: privately provide `KEYSTORE_PATH`, `KEYSTORE_PASSWORD`,
  `KEY_ALIAS`, `KEY_PASSWORD`; keep the existing production key outside this repository.
  With no `KEYSTORE_PATH`, release builds are unsigned. No keystore is included.
- build libowenclavecore: `./run lib core` or `./library/core/build.sh`
- build the required arm64 Slipstream sidecar: `./bin/lib/slipstream/build.sh`
- download assets: `./gradlew :app:downloadAssets`
- build owenclave: `./gradlew :app:assembleOssRelease`
- apk files are located in `./app/build/outputs/apk/oss/release`

links:
upstream    [owenewans/owenclave](https://github.com/owenewans/owenclave)
olcrtc      [openlibrecommunity/olcrtc](https://github.com/openlibrecommunity/olcrtc)
zapret2     [bol-van/zapret2](https://github.com/bol-van/zapret2)

---
author      [owenewans.org](https://owenewans.org)
email       [owenewans@owenewans.org](mailto:owenewans@owenewans.org)
tg          [t.me/owenewans](https://t.me/owenewans)
