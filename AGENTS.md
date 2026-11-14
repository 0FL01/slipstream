# Slipstream mobile

One root Git repository; `owenclave/`, `slipstream-c/`, `slipstream-rust/` are editable vendored trees, not submodules.

## Map
- `docs/source-provenance.md` and `docs/source-import.json`: original revisions and effective patches.
- `owenclave/docs/dns-tunnel.md`: actual Android carrier contract; FlowRelay belongs only to `owenclave/bin/lib/slipstream/flowd/`.
- `slipstream-c/bench/`: experimental native C benchmark, not the Android carrier.
- `docs/operations-boundary.md`: live authority and machine-specific DE connection prerequisites.

## Rules
- Read scoped AGENTS before editing. Keep the two picoquic revisions distinct; effective patches are already applied.
- Never reset/checkout/clone over sibling source or reapply Android patches during builds.
- Keep private keys, keystores, tokens, connection URIs, configs and generated binaries outside Git. Public certificate pins are deliberate source.
- Root `opencode.jsonc` defines only `ssh-n-de1`; a connection is not deployment authorization. DE frontend/backend also serve RA74.
- Operations repository remains authoritative for runtime. Historical goals/evidence do not establish current live state or authorize restarts.
- Nested workflows are inactive at root. CI/release/deployment activation needs separate scope.

## Verify
- Local commands and pinned Android prerequisites: [README](README.md).
- `git diff --check`; check staged files for secret/generated artifacts and mode160000 before committing.
- Preserve active multipath and blocked Jitsi goals under `owenclave/docs/goals/`; migration does not close them.
