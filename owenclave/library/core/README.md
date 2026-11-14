# Carrier-only libexclavecore

This directory contains the local core bound into Owenclave's `libexclavecore.aar`.
Its source and dependency pins are:

- `github.com/exclavenetwork/libexclavecore` at
  `v0.0.0-20260804212347-3ddce20eb15b`;
- `github.com/exclavenetwork/exclave-core/v5` at
  `v5.50.1-0.20260804210958-67d1a59820da`.

The non-Clash upstream libexclavecore source is copied here to preserve its exported
Go and generated Java API. Local changes replace the upstream monolithic config and
distribution imports with `internal/config`, an explicit registration graph for the
carrier runtime, local module imports, and local-module build scripts. No removed
protocol package may be added to the production dependency graph.

Run focused host checks with:

```sh
CGO_ENABLED=0 go test ./...
go mod verify
```

The normal Android build remains `./build.sh` in the repository's supported Nix
development environment. It uses the pinned module graph in `go.mod` and writes the
same `libexclavecore.aar` consumed by the Android project.
