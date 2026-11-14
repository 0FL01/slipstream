#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
SRC="$ROOT/bin/lib/slipstream/flowd"
OUT="${1:?usage: build-flowd.sh OUTPUT}"
if [[ "$OUT" != /* ]]; then
  OUT="$PWD/$OUT"
fi

command -v go >/dev/null
[[ "$(go env GOVERSION)" == go1.26.* ]]
mkdir -p "$(dirname "$OUT")"
(cd "$SRC" && CGO_ENABLED=0 GOOS=linux GOARCH=amd64 \
  go build -trimpath -buildvcs=false -ldflags='-s -w' -o "$OUT" .)
sha256sum "$OUT"
