# Goal: RU→DE direct через пять DNS multipath

> Historical operations record. Original paths/observations below are retained, not live claims. [Boundary](../operations-boundary.md) · [retained evidence](../evidence/ru-de-five-dns-20261030/) · [current C bench](../../slipstream-c/bench/README.md).

Status: complete
Source: user: «Попробуй ещё добавить 8.8.8.8 , 1.1.1.1 , 9.9.9.9, opendns, 223.5.5.5 как multipath / жду результат, потом обратно прод рабочий вернуть на n-de1».
Last updated: 2026-10-31 (measurement timestamps below are UTC on2026-10-30)
Previous completed experiment: [1/2/3-path result](2026-10-30-ru-de-slipstream-multipath.md).

## Objective and execution directive

Проверить одновременно заданную пятёрку в одном Slipstream QUIC-соединении RU→DNS→DE, с прежним требованием DE direct для тестового payload, измерить полезный throughput и вернуть текущий рабочий production DE. Прирост не обещан.

Complete the frozen Required Outcomes using the listed Change Envelope and Primary Evidence. Work on the smallest unresolved outcome. Do not add requirements from reviews, tests, tools, speculative risks, or optional source text. Finish when every required outcome is resolved and affected constraints remain satisfied. Stop substantive work at a proven external blocker, an approved budget boundary, or when no remaining in-scope action has a falsifiable expected result; record the exact evidence and smallest unlock.

## Frozen contract

- R1: Подготовить настоящее пространство для пяти native путей и bounded benchmark.
  - Source: explicit five-resolver multipath request.
  - Acceptance: endpoints8.8.8.8,1.1.1.1,9.9.9.9,208.67.222.222(OpenDNS),223.5.5.5 accepted together; initial QUIC path-ID ceiling allows IDs0–4 on both endpoints. Existing1/2/3 behavior/security retained; native/parser checks pass. Distinguish configured endpoints, responsive adapters and proven path use.
  - Primary evidence: minimal C/runtime/bench delta, focused five-path runtime and existing native suites, binary hashes.
  - Status: verified
  - Evidence: initial_max_path_id4 at both runtimes; targeted Python parser/allocation3/3; offline Debian12 release6/6 native suites, runtime17/17 including five-endpoint primary-loss/payload/lifecycle. Separate old TP2-client→new TP4-server existing loss/idle/payload test passed9.002s. Optional fifth-only delivery after simultaneous loss of first4 did NOT complete within10s; that resilience and per-path useful-byte attribution remain unproven. Endpoint query counts alone are not validation proof.
- R2: Измерить requested five-way multipath versus contemporaneous best working single, DE direct.
  - Source: requested trial/result; previous explicit direct-only clarification remains applicable.
  - Acceptance: short sequential real-payload screen of these5 resolvers; full five-way versus best working single at equal224 workers/64 queued queries, two AB/BA pairs. Keep all failed paths/windows and per-adapter aggregate counters. Six fresh confirmation pairs only if both preliminary DL pairs win with complete windows and fit the fixed budget; otherwise explicitly no confirmed promotion.
  - Primary evidence: exact acknowledged/hash-checked fixture bytes, full JSONL and summary, external direct proof and native resource/adapter aggregates.
  - Status: verified
  - Evidence: valid trial job`1793393890725-5`,20:58:14–21:02:23Z, exit0/4m09s within1800s; pool8/10 exact payloads succeed(AliDNS2 timeouts), full matrix4/4 complete and48/48 foreground requests pass. Five configured endpoints exchange DNS in both candidate windows, but no per-path useful-byte attribution; no repeatable DL win(1/2 pairs), no confirmation. Direct TCP/UDP, old-token WARP bound-reference, unknown/private/backend-down fail-closed proofs pass. Full results below and verified aggregate archives retained.
- R3: Вернуть рабочий production DE и убрать эксперимент.
  - Source: «потом обратно прод рабочий вернуть на n-de1» and safe-change workflow.
  - Acceptance: fresh protected backup; original installed Slipstream/FlowRelay binaries, base units, credentials/certificate/reset seed and static firewall untouched; temporary override binaries/services/scoped rule removed; restored original-token WARP TCP/UDP and recovered RA74 root SSH; RU children/temporary credentials absent.
  - Primary evidence: original-file/stateless-rule comparison, service/listener metadata and authenticated existing-path probes after restoration.
  - Status: verified
  - Evidence: original frontend restart21:05:40Z, installed9 files/full stateless nft match backup. Original FlowRelay394304/Slipstream394305 active/NRestarts0/null output/no drop-ins; WARP PID599 never restarted. Old-token WARP TCP/UDP and retired test-token rejection pass; strict-pinned RA74 root SSH recovered after BOTH shared boundaries. All test listeners/units/rule/binaries/credential mounts and RU/DE workspaces removed;7 local private credential/key/bundle files removed after archive verification. Original executable paths checked live.

## Constraints and change envelope

- OpenDNS means its primary public endpoint208.67.222.222:53; no second OpenDNS leaf or unrelated resolver sweep. Reuse existing persistent TCP DNS adapters; one child/QUIC connection, independent loopback adapter per endpoint, aggregate queue64.
- Local C checkout already contains the completed earlier task's4-file diff and pre-existing derived picoquic dirtiness. Preserve all of it. Extend only native default TP capacity to5, nearest benchmark limit/docs/tests and a separate ignored release build. No scheduler/CC/picoquic submodule edit, new dependency or public/generic API.
- Current picoquic default initial_max_path_id2 permits initial IDs0–2; accepting5 CLI arguments alone is not proof of5 usable paths. User now explicitly authorizes this minimal additional native path-capacity change. Report any unsupported loss-recovery or live resolver limitation rather than broadening implementation.
- Temporary DE experimental Slipstream executable selected only by a runtime drop-in if required; NEVER overwrite installed production executable. Same original cert/key/reset seed/domains/target and resource bounds. Reuse existing optional FlowRelay token dispatch + private eth0-bound direct backend/size-hash fixture and one exact input allow rule; no public fixture firewall opening, no fallback.
- Two shared frontend restart boundaries: deploy experimental override, restore original. Do not restart/launch WARP or change its namespace/routes. Keep original token WARP-only; test credentials distinct and stdin-only at client. Recheck RA74 after both boundaries; respawn may temporarily remove reverse listener.
- RU temporary `/opt/slipstream-mp-bench/` only; no Xray/routing/system DNS edits. DE `/run/slipstream-mp-bench/`, runtime unit drop-ins, protected timestamped backup and separate temporary executables only. No persistent traffic/QNAME/payload/native logs, keys/tokens in output, commit or push.
- Native output null; temporary backend/fixture runtime<=1h and existing64MiB/50% CPU bounds. Measurement budget<=1800s from first resolver screen, no silent extension; preserve partial results, remove test runtime and report limitations if exhausted. No permanent production multipath cutover, LTE/Android change or global-maximum claim.

## Current checkpoint

- R1–R3 and final contract/diff/privacy closure complete.
- Next: none. Stop; no further runtime, native changes or performance experiments.

## Current state

- Previous goal remains complete; its archived results are not new samples.
- Local operations has only previous untracked goal; C has previous4-file edits/pre-existing derived submodule state; Owenclave clean. Existing tested FlowRelay binary/probe/fixture sources may be reused, but all test credentials will be regenerated.
- Fresh protected backup:`/root/backups/ru-de-slipstream-five-20261030T203650Z/`; original9 files copied/compared and hashes retained internally, stateless nft/runtime metadata saved. Original Slipstream`b7c6b7ce4905adde4108b820115f1fce538d4afa79782fba6745203e5d2b3702`, FlowRelay`eb07424fe9cee1102c136ec6ef6884388e93806174816ce5d3b3906bcd511a6e`; WARP PID599 remains unchanged.
- Fresh old-token WARP TCP/transaction-checked UDP bound-reference proof and strict-pinned RA74 root SSH passed before changes, under the experimental server, and after restoration. Experimental391246/391245 replaced at21:05:40Z by original Slipstream394305/FlowRelay394304, actual executable paths verified; no permanent multipath deployment remains.
- R1 source delta raises both default TP path IDs to4 and benchmark override to5. Initial build was blocked by deleted image; reconstructed project-declared Debian12 tools only inside local image`slipstream-mp5-build:bookworm`, SHA256`038ca8d85c7af0635f1224016df69ba2ef8ebdb81a2c6612b9ca7e876fa29a98`(base bookworm-slim`99c0b25c46d0214d5a2091092e7908907dcaecd6a962cde0ac700acee222b6f3`, GCC12.2/Meson1.0.1/OpenSSL3.0.22/glibc2.36). Offline native build/tests passed; no VPS package install/new runtime dependency or SDK change.
- Built client SHA256`b8f566022eeb7ebf7fdc0c50972640f27ae886bc6b0d66e7665e13437c73676b`; server`d446789d460d1affaa7016ab0a699deba1aff5870d3e4ad734183b20aa1e7aee`, under`tmp/slipstream-c/build-ru-de-five-20261030/`. Temporary FlowRelay reused verified hash`18588aed884fc48ab7d8ab0761d64e9e817fcfed085359cb6571bfd0ccc412a2`.
- Component/unit/scoped nft gates passed before activation. Restoration removed BOTH runtime frontend overrides and selected original binaries. Its immediate post-restart`exe` assertion raced Type=simple execution and exited after originals were already active; inspected exact stage and resumed remaining proof/owned cleanup WITHOUT another frontend restart. Original-token WARP TCP/UDP, protected9 files and full stateless runtime rules comparison passed; archived restoration-note records this rather than hiding the interrupted script. Test listeners40003/40004 and all temporary unit/drop-in/binary/credentials paths absent.
- One setup error is excluded, NOT hidden: invoking the temporary no-argparse driver with`--help` accidentally started it before experimental frontend activation. Owned driver was terminated, zero children survived; data/decision moved to RU`preflight-invalid/` with classification. Valid measurement starts from fresh root-level files only after direct/production proofs. Legacy-compatibility wrapper initially used DEVNULL where fixture required empty PIPE output; only wrapper corrected, final gate passed, no production code changed for it.

## Evidence and results

### Fresh individual resolver screen

One real-payload window per requested resolver,56 TCP workers/64 queue, pinned authenticated DE direct. DL512KiB and UL32KiB; decimal Mbit/s. These short screens choose the contemporaneous control, not sustained operating capacity.

| Requested TCP53 endpoint | DL Mbit/s | UL Mbit/s | Outcome |
|---|---:|---:|---|
|8.8.8.8|4.784|0.370|both payloads pass|
|1.1.1.1|1.293|0.088|both payloads pass|
|9.9.9.9|1.757|0.158|both payloads pass|
|208.67.222.222(OpenDNS)|5.100|0.422|both payloads pass; selected control|
|223.5.5.5(AliDNS)|not completed|not completed|both payload requests fail; DNS replies alone are not success|

AliDNS nevertheless returned1085 adapter-accepted DNS replies, with1032 exchange errors/1031 retries over43.3s. Both payloads hit their20s deadline(TimeoutError, acknowledged0). This is NOT zero physical bandwidth or proof of useful path validation. No provider filtering or TCP-error root cause was established.

### Full matched comparison

Best working OpenDNS single versus ALL five requested endpoints together,224 workers/64 queue, one pinned authenticated native child/QUIC connection per window. Five-way split is45/45/45/45/44 workers and13/13/13/13/12 queue slots. Two balanced AB/BA pairs; each arm has only2 windows. Useful acknowledged bytes, not DNS wire bytes; decimal Mbit/s:

| Metric(median) | Single OpenDNS | Five configured resolvers |
|---|---:|---:|
|DL1MiB|5.461|4.437|
|UL512KiB standalone|1.112|1.088|
|UL512KiB with replenished1MiB DL|0.725|1.099|

- DL pairs:6.749→3.524 Mbit/s(−47.78%) and4.173→5.349(+28.19%);1/2 wins. Median paired change−9.80%(different estimator from ratio of column medians). No repeatable additive DL gain; six-pair confirmation correctly not entered.
- Loaded512KiB UL finished in5.939/5.640s single versus4.082/3.585s five-way, an interesting observed improvement in BOTH small screening pairs, but not an independently confirmed operating profile. Across the four loaded UL sizes the descriptive completion p95 is5.939 versus4.082s; NOT network RTT or a large-sample percentile. This does not create another experiment/outcome.
- All4 full windows and48 foreground requests succeeded; background transfers are retained separately. Nine valid cleanup records have no child/worker survivors; independent process check agrees. Maximum sampled native-client RSS11931648 bytes(11.379MiB), one thread; excludes Python adapters/kernel buffers.
- All5 adapters replied in both five-way windows, but AliDNS had only3 queries/3 replies in the first, versus941/941 with897 exchange errors in the second. Other four have thousands of replies. Do NOT claim stable useful aggregation across every one of the5 paths; per-path payload share was not measured. Optional synthetic fifth-only/four-loss test remains unproven, as recorded under R1.
- Invalid setup preflight is separately archived with0 workload request rows,4 cleanup records/3 failed startup summaries. It is excluded from ALL valid resolver/window metrics, not reclassified as a provider failure.

### Retained evidence and restoration

- Local aggregate directory:`.tmp/ru-de-slipstream-five-20261030/`; `final-metrics.json`, `ru-results/`, `de-evidence/` are secret-free. Both archive hashes and ALL15 RU manifest entries verified locally before remote teardown.
- `ru-aggregate-results.tar.gz`:33116 bytes, SHA256`91a04dc9632e29a84a3fa14453dffd66a164eadced88ebc56fc8be6fe6c53712`(16 files incl manifest and classified invalid preflight; no credentials/key/bundle).
- `de-aggregate-evidence.tar.gz`:4083 bytes, SHA256`5d21693a272dfefc9611c7c5279567eaeb970711ac379b086169a9394aecc6a3`; also protected backup`/root/backups/ru-de-slipstream-five-20261030T203650Z/aggregate-evidence.tar.gz`. No full config, private key or token in archive.
- Exactly2 shared restart boundaries:20:57:41Z experimental deployment,21:05:40Z original restoration. The restoration guard failure did not cause a third restart. RA74 listener alone was not accepted as proof: fresh strict-pinned root SSH returned UID0/OpenWrt under the experiment AND after restoration. Rescuer respawn can cause a recovery gap; uninterrupted availability is not claimed.
- Installed original binaries/base units/cert/key/token/reset seed/nftables file unchanged; full stateless runtime nft matches baseline. Public UDP53/private40001 retained; test40003/40004 absent. Original-token WARP TCP/UDP remains fail-closed, retired test token rejected. WARP daemon stays PID599/since2026-10-19, host default direct. RU default route/system DNS unchanged.
- RU`/opt/slipstream-mp-bench/` and DE`/run/slipstream-mp-bench/` removed after verified copying, including fresh test credentials/private fixture key. Temporary executables/units/drop-ins/rule/credential mounts absent;7 local secret/key/bundle files purged. Protected original-production backup and secret-free evidence retained only.
- Local C now has6 intended changed files(previous4 plus two TP assignments); pre-existing picoquic derived diff preserved. Owenclave unchanged, no commit/push, no permanent routing/production cutover or sustained/global-maximum claim.

## Completion

- Resolved outcomes: R1–R3 verified with the source/runtime/negative/performance/restoration evidence above.
- Closure: operations/C`git diff --check`, explicit untracked-goal whitespace/3-verified-outcome check, prescribed operations secret scan and local private-file absence checks passed. Final diff reviewed against this request:6 intended C files plus unchanged pre-existing picoquic dirtiness(hash`4fe08d92519722ea55aa50a59e0facb87202a30fe652e1884d31a3c951f481bc`); only new task goals in operations, Owenclave clean. No commit/push.
- Final status: complete. Result is a bounded negative DL screening result with observed loaded-UL improvement, NOT confirmed additive five-path capacity. Original DE production is restored and authenticated live, all experimental runtime/credentials removed.
