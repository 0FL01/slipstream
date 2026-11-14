# Operations boundary

Live runtime authority remains `/home/stfu/ai/work-table/space` (ass-peak-proxy operations), particularly `docs/n-de1.md` and `docs/access-paths.md`. This source repository does not replicate topology or own live configs. Read those current documents and exact remote consumers before any separately authorized deployment.

DE Slipstream and FlowRelay are shared with RA74 reverse administration. Restarting either may interrupt that control path. Source access, MCP availability and historical experiment approval grant no general deployment/restart authorization. This migration performed no remote writes or service changes.

## Project OpenCode connection
`opencode.jsonc` copies only the working operations `ssh-n-de1` server object into V2 `mcp.servers`. No other project host is defined. Global/ancestor OpenCode settings can still merge independently.

Machine prerequisites: `/usr/bin/ssh-mcp`, resolution/SSH configuration for `n-de1`, port41924, root account, external private key `/home/stfu/.ssh/chert`, and trusted host-key data for that endpoint. These paths are machine-specific; provision equivalent external references on another workstation. No credential contents are embedded. The copied command has no host-key bypass; retain the working ssh-mcp host-key verification defaults and trusted-host setup. Do not use insecure host-key options to bootstrap a new host.

Verification during migration: OpenCode discovered the project config and `opencode mcp list` in this repository reported `ssh-n-de1` connected after project initialization. The matching SSH MCP connection returned `n-de1` for read-only `hostname`. This proves connectivity/identity response at that moment, not carrier health or authorization to modify services.

## Historical records
Transferred goals under `docs/goals/2026-09-27-slipstream-*` and `2026-10-30-ru-de-slipstream-*` retain original observations, paths and status. Paths beginning `tmp/`, `.tmp/`, `/opt/`, `/run/`, and operations-relative node references describe the original environment; they are not current local instructions. Only approved aggregate evidence is mirrored under `docs/evidence/`. `SHA256SUMS` records exact copied bytes; RU internal manifest members were verified against their archives. Archived scripts are evidence, not deployment entry points.

The RU–DE multipath trials did not establish repeatable additive download gains. Production restoration/RA74 recovery describe experiment closure in October, not current live validation. C five-path support remains experimental; Android's Rust carrier remains actual source.
