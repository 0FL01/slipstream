#!/usr/bin/env bash
set -euo pipefail
ROOT=$(git rev-parse --show-toplevel)
HERE="$ROOT/bin/lib/masterdns"
PIN=acbf1c61f90786f41b975d2e2f616afbce292b29
SOURCE="$ROOT/build/masterdns/$PIN"
OUT="$ROOT/build/masterdns/artifacts"
mkdir -p "$OUT"
if [[ ! -d "$SOURCE/.git" ]]; then
    git clone --no-checkout https://github.com/masterking32/MasterDnsVPN.git "$SOURCE"
    git -C "$SOURCE" checkout --detach "$PIN"
    git -C "$SOURCE" apply --check "$HERE/managed.patch"
    git -C "$SOURCE" apply "$HERE/managed.patch"
fi
[[ $(git -C "$SOURCE" rev-parse HEAD) == "$PIN" ]]
# Never reset a worktree or discard local experiments. A mismatched patch stops.
git -C "$SOURCE" apply --reverse --check "$HERE/managed.patch"
cp -R "$HERE/overlay/." "$SOURCE/"
export GOTOOLCHAIN=go1.26.5
if [[ $(go env GOVERSION) != go1.26.5 ]]; then
    printf '%s\n' 'Go 1.26.5 is required for the pinned build' >&2
    exit 1
fi
(
    cd "$SOURCE"
    go test ./internal/flowbridge ./internal/client ./internal/arq ./internal/udpserver ./internal/config
    CGO_ENABLED=0 GOOS=linux GOARCH=amd64 go build -trimpath -buildvcs=false -ldflags='-s -w' -o "$OUT/masterdns-server" ./cmd/owenclave-server
    CGO_ENABLED=0 GOOS=linux GOARCH=amd64 go build -trimpath -buildvcs=false -ldflags='-s -w' -o "$OUT/flowtls" ./cmd/owenclave-flowtls
    CGO_ENABLED=0 GOOS=android GOARCH=arm64 go build -trimpath -buildvcs=false -ldflags='-s -w' -o "$OUT/libmasterdns.so" ./cmd/owenclave-client
)
mkdir -p "$ROOT/app/src/main/jniLibs/arm64-v8a"
install -m 755 "$OUT/libmasterdns.so" "$ROOT/app/src/main/jniLibs/arm64-v8a/libmasterdns.so"
sha256sum "$OUT/masterdns-server" "$OUT/flowtls" "$OUT/libmasterdns.so"
