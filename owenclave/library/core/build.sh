#!/usr/bin/env bash

CGO_LDFLAGS="-Wl,-z,max-page-size=16384" gomobile bind -v -target=android/arm64 -androidapi 21 -trimpath -ldflags="-s -buildid=" "github.com/owenewans/libowenclavecore" || exit 1

proj=../../app/libs
if [ -d $proj ]; then
  cp -vf libexclavecore.aar $proj
fi
