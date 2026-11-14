# Goal: Slipstream upload-first

> Historical operations record; original paths and observations remain below. [Current boundary](../operations-boundary.md) · [current C benchmark](../../slipstream-c/bench/README.md).

Status: complete
Source: user-approved action plan and instruction to execute iteratively, without rebuilding Android
Last updated: 2026-09-27

## Objective
Reduce confirmed upload stalls using measured, isolated Slipstream changes; retain credentials and production safety. Recommend an APK rebuild only if client-side changes show profit.

## Execution Directive
Complete the frozen Required Outcomes using the listed Change Envelope and Primary Evidence. Work on the smallest unresolved outcome. Do not add requirements from reviews, tests, tools, speculative risks, or optional source text. Finish when every required outcome is resolved and affected constraints remain satisfied.

## Frozen Contract
- R1: Copy the approved plan and establish fresh upload baseline with unchanged Rust client and current C server.
  - Source: user asks for plan copied into goal and local Python testing.
  - Acceptance: acknowledged upload measurements, standalone and loaded, distinguish server-only from client changes.
  - Primary evidence: local Python synthetic A/B aggregate results.
  - Status: verified
  - Evidence: `tmp/slipstream-c/bench/upload-first-report.md`: fresh direct A/A and AB/BA, corrected 100 ms TCP-DNS model with unchanged Rust client, acknowledged sizes and sustained 4 MiB. Model is synthetic, not real recursive/LTE; actual carrier remains an explicit limitation.
- R2: Iteratively evaluate confirmed empty-response bug, ACK/credit and, if necessary, bounded poll recovery.
  - Source: approved plan, upload prioritization with acceptable DL sacrifice.
  - Acceptance: isolate each candidate, retain or reject based on evidence; document successes and failures.
  - Primary evidence: targeted regression tests and paired Python measurements.
  - Status: verified
  - Evidence: isolated NOERROR regression passes; corrected-model NOERROR screen misses ≥20% gate; native server ACK/credit and C-client poll diagnostics do not reproduce sustained starvation; same-model C/Rust and BBR/DCUBIC screens reject performance promotion.
- R3: Deliver the verified server result or documented limitation and an evidence-based APK recommendation.
  - Source: execute plan; no Android rebuild now, propose only if profitable.
  - Acceptance: preserve credentials/pin/Flowd WARP boundary, safe production rollback; no Android changes; exact limitations reported.
  - Primary evidence: component checks, production Rust-client TCP/UOT checks if deployment changes, final diff and result.
  - Status: verified
  - Evidence: no performance candidate met screening, so production server remains b7c6b7ce...; Slipstream/Flowd active, NRestarts 0/0, null outputs. WARP restart counts/timestamps unchanged. No APK recommendation from unconfirmed 9% client screen; no deployment, commit or push. Local release 6/6 and Python regressions 2/2 pass.

### Constraints / Non-goals
- No Android rebuild or Android source edits. No real Telegram traffic logging, QNAME/payload/key logs, or credential output.
- Preserve cert/key, Flow token, reset seed, suffixes, public-only targets, interface-bound fail-closed Flowd and isolated WARP.
- No unrelated routing/DNS changes. Completed prior goals remain historical.
- Existing ops working-tree documentation changes and nested derived picoquic changes must not be reset.

## Change Envelope
- `tmp/slipstream-c`: targeted codec/runtime/picoquic patches, closest tests and local Python benchmark.
- Isolated Rust Linux experiment only if client hypothesis requires it; never dirty Owenclave source/cache.
- n-de1: backed-up server-only deployment after checks; ephemeral bounded fixtures only if necessary, null output and cleanup.
- This goal and affected node docs; commits/push only for measured profit under original publication permission.

## Approved Plan (copy)
1. Baseline: unchanged Rust client → current C server; exact acknowledged 32/128/512 KiB and sustained 4 MiB upload, standalone/loaded download, responsiveness. Direct and current recursive carrier separately. Record completion time, interval goodput/stalls, loaded p95/errors, DNS counts/empty/NXDOMAIN. Synthetic in-memory counters may identify cwnd/pacing/in-flight/ACK/credit, never actual traffic metadata.
2. Fix issue #15 independently: valid zero-QUIC response becomes NOERROR+empty, malformed requests retain errors. Do not silently drop replies. Test codec, old Rust client, recursive carrier, TCP/UOT and old pin/token; repeat baseline. Server-only iteration needs no APK change.
3. Diagnose server ACK/credit: existing no_ack_delay=1 is already set. Verify nearest-response ACK/MAX_STREAM_DATA service and actual drained-byte credit. Trial ACK/credit priority versus download only if competition is reproduced. Window/credit tuning is a separate candidate; prior larger-window trial did not help locally.
4. If ACK starvation remains, reproduce bounded client recovery: sticky poll until emitted; allow rate-limited non-ACK-eliciting control-only poll outside normal pacing/cwnd only for outstanding stalled upload. STREAM remains congestion-controlled; bound busy DNS operations and disable recovery after progress/idle. RTT/4 with bounds is an experimental starting point, not a production promise. Validate in C and then isolated Rust Linux; recommend APK update only for demonstrated effect.
5. Two fresh screening AB/BA pairs per candidate; promising candidate gets six AB/BA confirmation pairs. Proposed gate: loaded acknowledged-upload time improves ≥20%, ≥5/6 wins, no new failures/stalls/auth/pin regressions, no persistent idle query increase. Initially allow DL loss ≤30%; larger loss requires explicit decision. BBR/DCUBIC can be an isolated trial, not a substitute for fixing starvation.
6. Backup current C before each production switch; validate and keep immediate rollback. Credentials/config/Flowd remain unchanged. Linux evidence is not Telegram/LTE proof; final Telegram test uses same resolver/network and equal byte payload, not recording duration alone.

## Current Checkpoint
- Closed: R1–R3. No next implementation checkpoint.
- Stop: no measured candidate passes the frozen promotion gate; sustained ACK starvation was not reproduced. Real selected recursive resolver/LTE/Telegram remains unmeasured, not evidence for another speculative patch.

## Current State
- Production C commit d7cd5555, SHA256 b7c6b7ce4905adde4108b820115f1fce538d4afa79782fba6745203e5d2b3702; unchanged Rust APK client.
- Confirmed source: server empty response becomes NXDOMAIN; client poll insertion lies inside pacing/cwnd gates; C clears failed poll requests.
- Original binary preserved as `build-upload-baseline/slipstream-server`; NOERROR-only candidate SHA256 b9aedcd05127903f896a1716f45a621985d189e660f24220d0e6a48e8a832c75 in `build-upload-noerror` and `build-candidate`.
- Runtime diff removes NXDOMAIN fallback for valid empty response only. Diagnostics are separate test executables, not production instrumentation.
- No production deployment, Android source changes or rebuild, commit or push for this objective.

## Checkpoint History
- 2026-09-27: frozen user-approved server-first plan, no Android rebuild.
- 2026-09-27: first probe's shared UDP observer assigned concurrent responses to the last peer. Delayed results invalidated; fixed reply ownership and added two reverse-order concurrent-peer regressions. Initial cold/4-second deadline measurements were not treated as upload-completion evidence. Corrected run warms up with acknowledged 1 KiB and allows 60/180 seconds.
- 2026-09-27: issue #15 confirmed and fixed locally; valid empty NOERROR and malformed/out-of-suffix NXDOMAIN regression passes. Independent release gate 6/6; Python reply-owner regressions 2/2. No performance claim follows from DNS correctness.
- 2026-09-27: server pacing gates did not deny ACK/control service in synthetic probes; credit advanced on drain. C-client 100 ms synthetic UDP, 0%/2% loss did not show sustained poll-gated ACK stalls. No speculative bypass patch retained.
- 2026-09-27: identical bounded TCP-DNS model, C versus Rust: loaded 512 KiB median 9.2645 versus 10.2191 s, paired reduction 9.14%, 2/2 wins; standalone practically equal. One sustained loaded 4 MiB pair 72.6525 versus 81.8879 s (11.28% shorter), not confirmation. Gate fails; no APK recommendation.
- 2026-09-27: unchanged Rust BBR versus DCUBIC isolated screen: loaded 512 KiB BBR 14.587/13.809 versus DCUBIC 10.298/10.187 s, median paired 38.61% slowdown, 0/2 wins; concurrent DL loses 34.70%/30.49%. BBR rejected, no confirmation or client changes.
- 2026-09-27: closure: installed production SHA unchanged, Slipstream/Flowd active with NRestarts 0/0 and null sinks; WARP timestamps/restarts unchanged. Local `.cache/` empty and no upload probe/native diagnostic processes remain. Earlier interrupted probe may have left a synthetic certificate directory in denied `/tmp/upload-first-*`; inspection/purge is forbidden by tool policy, so no cleanup claim for that path. No production credentials were stored there.

## Limits and interpretation
- Corrected synthetic TCP model saturates 56 workers / 64 queued operations at 100 ms per exchange: approximately 560 completed exchanges/s before overhead. Client→server Base32/QNAME payload capacity is much smaller than TXT response capacity. Nearly identical standalone C/Rust results are consistent with a carrier service bound, not proof that native polling caused Telegram's slowdown.
- A real resolver/LTE may have different delay, loss, admission and caching. No throughput or Telegram improvement is claimed for it. A future real-carrier checkpoint needs the selected profile resolver/network and byte size, rather than voice duration alone.
- NOERROR is a verified correctness change kept locally, not a speed patch or published release. Runtime credentials/configuration and previous production deployment remain intact.

## Completion
- Resolved outcomes: R1–R3 with experiments, negative results and exact limitations above.
- Commands/artifacts: `meson test -C build-candidate --no-rebuild --print-errorlogs` in Debian build image (6/6); `python3 -m unittest discover -s bench -p test_upload_first.py` (2/2); `bench/upload-first-report.md` and ignored aggregate JSONL; remote `systemctl show` and installed binary SHA.
- Constraint/scope check: only isolated DNS correctness runtime edit plus tests/Python evidence and this goal; Android untouched, native STREAM congestion guards unchanged, no extra production service/logging, no speculative POLL bypass. Existing unrelated working-tree changes preserved. No performance promotion, commit or push.
- Final status: complete evidence campaign; requested speed improvement remains unproven, explicitly not represented as achieved.
