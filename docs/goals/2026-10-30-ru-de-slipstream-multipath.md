# Goal: измерить RU→DNS multipath→DE direct

> Historical operations record. Original paths/observations below are retained, not live claims. [Boundary](../operations-boundary.md) · [retained evidence](../evidence/ru-de-multipath-20261030/) · [current C bench](../../slipstream-c/bench/README.md).

Status: complete
Source: user: «Сначала замер, но без warp, шо бы выход в direct был»; затем «Реализовать, и логируй результаты в цель / жду результат». Approved conversation plan: bounded temporary 1/2/3-path benchmark, no production routing cutover.
Last updated: 2026-10-30

## Objective

Получить воспроизводимое измерение полезной DE→RU и RU→DE пропускной способности одного Slipstream-соединения через несколько recursive DNS, сравнить с лучшим single resolver при одинаковых ресурсах, подтвердить DE direct и полностью снять эксперимент. Прирост не обещан: отрицательный результат сохраняется.

## Execution Directive

Complete the frozen Required Outcomes using the listed Change Envelope and Primary Evidence. Work on the smallest unresolved outcome. Do not add requirements from reviews, tests, tools, speculative risks, or optional source text. Finish when every required outcome is resolved and affected constraints remain satisfied. Stop substantive work at a proven external blocker, an approved budget boundary, or when no remaining in-scope action has a falsifiable expected result; record the exact evidence and smallest unlock.

## Frozen Contract

- R1: Подготовить bounded single/2/3-path Linux benchmark, переиспользуя текущий C Slipstream и TCP DNS adapters.
  - Source: approved plan, steps 1–2.
  - Acceptance: explicit 1–3 unique IPv4 resolver endpoints, aggregate workers56/112/224 and queue64, pin/auth/stdin retained; nearest parser and three-path runtime tests pass.
  - Primary evidence: `tmp/slipstream-c` diff, Python/Meson results and client artifact hash.
  - Status: verified
  - Evidence: local14 Python tests passed; narrowed final parser/allocation checks3/3 passed after removing redundant input permutations. Offline Debian12 release6/6 Meson suites, runtime16/16 including separate2/3-path primary-loss recovery. Client SHA256`8825529fe87cb2c06e4adb552e1b95a8e6896db17c54a4a0dbedee5d7ea55f7e`, `tmp/slipstream-c/build-ru-de-20261030/slipstream-client`. Native source/picoquic unchanged.
- R2: Временно получить RU→recursive DNS→DE direct без переключения рабочего RU routing; production token остаётся WARP-only.
  - Source: user direct-only clarification and approved plan, steps 3–4.
  - Acceptance: separate test token/private eth0-bound backend, external direct egress proof; old-token WARP and RA74 consumer preserved; wrong token/pin, private destination and stopped test backend fail closed.
  - Primary evidence: bounded authenticated end-to-end probes and runtime metadata, no traffic logs.
  - Status: verified
  - Evidence: first shared switch17:41:43Z. Production old-token TCP remains`warp=on`; UDP matches fresh CloudflareWARP-bound STUN reference. Test token TCP/transaction-checked UDP proves DE direct`195.128.101.186`/`warp=off`; unknown token/private target and stopped direct backend close without WARP fallback. RU→Google recursive DNS→external HTTPS also proves that direct address; wrong carrier pin refuses payload. RA74 reverse listener was briefly absent after restart, then recovered automatically; fresh strict-pinned SSH returned root. Do not claim uninterrupted administration.
- R3: Измерить resolver pool, single versus multipath and bounded worker scaling; записать успешные и неуспешные результаты.
  - Source: initial maximum-throughput request and approved plan, step 5.
  - Acceptance: sequential per-resolver payload screening; best working single versus2/3 paths at equal56/64;112/224 screen with matched single control; two balanced screen pairs and six fresh confirmation pairs only for a credible candidate. Report acknowledged bytes, throughput, completion/errors and resources without universal-optimum claims.
  - Primary evidence: aggregate-only benchmark JSONL and summary in this goal.
  - Status: verified
  - Evidence:8-resolver pool16/16 payloads;28 full-window attempts,22 complete/264 successful foreground requests and6 Google carrier bootstrap failures retained. Matched Google/cross-provider/triple56 and Yandex112/224 screens show no complete candidate winning both DL pairs; no six-pair confirmation qualified. Recovered working-provider resource screens finished inside original deadline. Exact results/limitations below and aggregate archives verified locally.
- R4: Снять временный runtime и восстановить production boundary.
  - Source: approved plan, step 6 and safe-change workflow.
  - Acceptance: original DE binary/base units/persistent firewall preserved/restored; only scoped runtime rules removed; private test listeners/drop-in and RU children/credentials absent; original DNS and administrative consumer work, no traffic log sink or restart growth.
  - Primary evidence: backup comparison, final listeners/service state and authenticated existing-path probes.
  - Status: verified
  - Evidence: second planned shared restart18:17:46Z restored original frontend. All9 protected files and complete stateless runtime nft rules match preflight; test services/rule/drop-in/binary removed; production TCP WARP/UDP bound-reference proofs pass, retired test token rejected. RU/DE temporary workspaces, systemd test credentials and local temporary tokens/private fixture keys/bundle removed after verified archive collection;40 cleanup records and process check show no survivors. Final strict-pinned RA74 SSH returned root; listener22074 present. Slipstream/FlowRelay active, null output/no drop-ins/NRestarts0 at closure; WARP PID599/activation unchanged.

### Constraints and non-goals

- No RU Xray/routing/system DNS changes, permanent service, new public proxy, APK change, persistent traffic logs, secret output, commit or push.
- Production Slipstream executable/certificate/key/reset seed/domain/target and old Flow token unchanged; unknown/test failures never fall through to production WARP.
- Existing shared frontend still has lifecycle dependence on WARP units; “без WARP” here is test payload/destination DNS direct egress. Full WARP-independent carrier is not included.
- Two planned shared FlowRelay/Slipstream restart boundaries: dispatch deployment and restoration. WARP daemon/config/routes untouched. Recheck RA74 reverse administration.
- Native transport/scheduler and negotiated path limits unchanged. CLI accepts8 endpoints but current picoquic defaults initial_max_path_id2 (IDs0–2); first experiment1/2/3 only.
- Start with TCP DNS public resolver pool:77.88.8.88,77.88.8.1,77.88.8.8,1.1.1.1,1.0.0.1,8.8.8.8,8.8.4.4,9.9.9.9. No scanning unrelated resolvers or unbounded load.

## Change Envelope

- Local C repository: `bench/run.py`, `bench/test_bench.py`, `bench/README.md`, `tests/test_runtime.py`; separate ignored release build and aggregate artifacts. Existing derived picoquic state must not be reset.
- Existing Owenclave `bin/lib/slipstream/flowd/` and synthetic fixture reused without production API/source changes.
- RU temporary `/opt/slipstream-mp-bench/`: one pinned authenticated client owner/adapters, private test credential and public certificate only.
- DE timestamped backup; temporary direct FlowRelay `10.200.0.1:40003`, same-carrier test-token dispatch override, authenticated size/hash fixture`:40004`, narrowly scoped input exception only. Resource/runtime bounds and null native output.
- This goal is the authoritative result record. Update node docs only if stable topology unexpectedly changes; no permanent topology change is planned.

## Current Checkpoint

- Closed: R1–R4. No next execution checkpoint.
- Final documentation, secret and diff closure passed. Stop: first bounded experiment is complete; repeatable speedup was not demonstrated.

## Current State

- Pre-implementation local operations tree clean at`3e1f5c0`; Owenclave clean at`9c8c879`; C`16fdfe9` with pre-existing derived modified picoquic submodule only.
- Protected DE backup`/root/backups/ru-de-slipstream-mp-20261030T172426Z/`: original9 files including credentials retained internally, all copied bytes compared, runtime/static rules and service metadata saved. Original FlowRelay hash`eb07424fe9cee1102c136ec6ef6884388e93806174816ce5d3b3906bcd511a6e`; Slipstream`b7c6b7ce4905adde4108b820115f1fce538d4afa79782fba6745203e5d2b3702`.
- Existing FlowRelay source test-race/vet/build passed. Temporary binary hash`18588aed884fc48ab7d8ab0761d64e9e817fcfed085359cb6571bfd0ccc412a2`. Both config checks, systemd verify and narrow nft syntax check passed. Runtime override/rule/binary/services are now removed; protected original files and stateless nft runtime equal backup. WARP PID599/activation/NRestarts0 unchanged.
- Pool job`1793382258728-5` completed0 in35s; initial matrix`1793382599034-7` completed0 in12:36; working-provider continuation`1793383843003-11` completed0 in5:54. Exit0 means experiment execution, not payload acceptance. Both matrices finished before original18:31:39Z deadline; production restored18:17:46Z.
- Local ignored evidence:`.tmp/ru-de-slipstream-mp-20261030/ru-results/`, `de-evidence/`, `final-metrics.json`. RU archive SHA256`48ff365aa88548866668dffdbf91aacc68d567749451135f17044f63f30f2ea2`; DE archive`24c34fbf2d9a83aba68dfd5659144267de6c19fc3788eb9fd2f49950cdd2ed13`; both bytes and all27 RU manifest entries checked. DE aggregate-only copy also retained with protected backup. No credential/key/full-config files are in these archives.
- RU`/opt/slipstream-mp-bench` and DE`/run/slipstream-mp-bench` absent. No temporary unit/drop-in,40003/40004 listener, test binary or credentials remain. Public Slipstream UDP53/private production40001 retained; final RA74 root command passed after automatic reverse-listener recovery. No application traffic logging was enabled. Two deliberate shared restart boundaries are recorded, not hidden by zero automatic restart counters.

## Measurements

### Initial resolver pool (single path,56 workers /64 queue)

One window per resolver, sequential512KiB download and acknowledged32KiB upload through DE direct. Decimal Mbit/s below; this is selection evidence, not sustained throughput or confirmation. DNS exchange errors/retries are retained separately from application success.

| Recursive TCP53 | DL Mbit/s | UL Mbit/s | Payload failures |
|---|---:|---:|---:|
|77.88.8.88|3.079|0.270|0|
|77.88.8.1|2.868|0.261|0|
|77.88.8.8|3.379|0.247|0|
|1.1.1.1|1.007|0.069|0|
|1.0.0.1|1.169|0.079|0|
|8.8.8.8|4.535|0.373|0|
|8.8.4.4|4.534|0.266|0|
|9.9.9.9|2.208|0.125|0|

### Frozen full-workload comparison

- Control A:8.8.8.8. Candidates B: Google pair`8.8.8.8+8.8.4.4`(`gg`), different-provider pair`8.8.8.8+77.88.8.8`(`gy`), triple`8.8.8.8+8.8.4.4+77.88.8.8`(`ggy`). Each gets two AB/BA screening pairs at56/64.
- Fastest complete candidate at56 selects topology for112/224 worker screens; if none is complete, highest measured DL topology still gets the already-approved resource screens, with its failures retained. Each scale is compared to a single resolver at the SAME workers/queue budget.
- Primary: sequential exact1MiB DE→RU goodput. Existing workload also includes acknowledged8/32/128/512KiB uploads standalone/under DL, two-flow DL, recovery/idle and native resource peaks. Same client, pin, suffix, backend, deadlines and explicit client DCUBIC throughout. Public resolver identities are configuration; no traffic destinations/payload/QNAMEs logged.
- A candidate is credible for six NEW confirmation pairs only if both matched primary screening ratios exceed1 and all four full windows complete without unexpected failures. Pick the fastest absolute candidate among those; not a universal optimum. Final report must retain UL/loaded costs and every failure even if DL wins.

| Completed screen | Single DL Mbit/s (window median) | MP DL Mbit/s (window median) | Median paired DL change | DL wins | Failed windows |
|---|---:|---:|---:|---:|---:|
|gg,56 workers|4.241|4.718|+14.60%|1/2|0/4|
|gy,56 workers|4.506|4.060|−9.44%|0/2|0/4|
|ggy,56 workers|4.742|4.377|−5.38%|1/2|0/4|
|Yandex pair,112 workers|4.522|4.329|−3.05%|1/2|0/4|
|Yandex pair,224 workers|5.398|4.109|−23.08%|0/2|0/4|

All12 initial full windows passed, but no56-worker topology wins both matched DL pairs. The Google pair has the highest complete candidate DL median and therefore selected the topology for112/224-worker screens. `--server` labels identify experimental layouts only; actual DE server/backend artifacts do not switch between arms.

### Google carrier failure during resource scaling

- At112 workers the first full single/MP pair passed: DL986833/842447 B/s (7.895/6.740 Mbit/s); standalone512KiB UL160622/142065 B/s (1.285/1.137 Mbit/s). MP lost this valid DL pair by14.63%.
- Both windows of the second112-worker pair and all four224-worker windows then failed bootstrap: each adapter received10 native queries and accepted0 DNS replies. These are retained startup failures, NOT measured zero-bandwidth payload transfers. No confirmation candidate qualified.
- DE stayed healthy, all four services active/NRestarts0; repeat production WARP and test direct TCP/UDP proofs passed. RU cleanup reports zero client/worker survivors, RLIMIT_NOFILE1024 and max224 workers leave room within that bound. No OOM, service expiry or restart was observed.
- A bounded repeat at56 workers still failed Google carrier, while Yandex77.88.8.8 passed512KiB/32KiB at421487/25184 B/s. Ordinary example.com A lookup through both Google endpoints and Yandex passed over BOTH UDP and TCP (6/6). This narrows the problem to the Google tunnel/recursive path, not general Google DNS or DE/host routing; provider rate/filtering is a hypothesis, not a proven cause.
- Smallest continuation for R3: use the best currently working pool member77.88.8.8 as matched single control and its already-approved77.88.8.1 partner for112/224. Keep the original18:31:39Z hard deadline and unchanged fixture/native/adapter semantics. Preserve original failures. Run six fresh pairs only if a complete repeatable winner qualifies AND all12 windows fit before the original deadline; otherwise report unconfirmed limitation, not promotion.

### Working-provider continuation and result

Yandex77.88.8.8(single) versus77.88.8.8+77.88.8.1(pair), same aggregate resources and original full workload:8/8 full windows passed. Neither112 nor224 qualifies for confirmation; no candidate was promoted or left running.

| Workers | Single/MP standalone512KiB UL Mbit/s | Single/MP loaded512KiB UL Mbit/s | Single/MP loaded completion p95 seconds |
|---|---:|---:|---:|
|112|1.006 /0.835|0.790 /0.791|5.826 /5.631|
|224|1.528 /1.203|1.779 /1.162|2.371 /3.659|

The p95 is descriptive application completion across the four supported upload sizes, not network RTT; only two windows per arm. Native client peak sampled RSS across completed screens10.625MiB; this excludes Python adapters/kernel TCP buffers and is not total system memory. All40 cleanup records show zero surviving native children/adapter workers; independent RU process check found none. DNS counts include polling/retransmissions, not useful bytes; equal workers do not mean equal query load.

**Conclusion:** true one-connection DNS multipath is functional, but more resolvers did not establish repeatable DE→RU goodput improvement in this bounded run. Best complete repeated working-provider profile is single Yandex224: DL5.398 and standalone512KiB UL1.528 Mbit/s(window medians). The isolated Google112 single window reached7.895/1.285 Mbit/s, but subsequent startup failures prevent calling it a reliable operating point. Initial Google pair benefit reversed in the next pair; cross-provider and triple did not fix this. No six-pair candidate was warranted by the frozen screen criterion.

This is a synthetic between-host carrier measurement: the fixture is on DE itself to remove an unrelated Internet download server. External DE direct egress was proven separately. It is not long-duration sustained traffic, LTE acceptance, a global optimum, proof that all other allocations fail, or proof that provider throttling caused the Google event. Four/eight active paths and native scheduler changes were not tested.

## Material Decisions

- 2026-10-30: user explicitly chose first measurement with DE direct, not existing WARP egress; later authorized execution and goal logging. Use previously tested optional token dispatch, not a new delegation or transport.
- 2026-10-30: R3 Google resource screen became unreachable with zero accepted DNS replies; healthy DE/Yandex/ordinary Google DNS controls exclude a general backend outage. Continue only existing112/224 budget screens on currently working Yandex pool endpoints, with no time/service/source scope expansion.

## Checkpoint History

- 2026-10-30: contract frozen before implementation; local runner and remote direct-boundary preparation are independent workstreams.
- 2026-10-30: R1 passed. R2 preflight/private backend passed. Preparation-only tool errors (cp parents, missing drop-in directory, escaped inline HTTP, SFTP root/timeout) were recovered before production restart; neither proxy credentials nor native traffic output were emitted. Test fixture/direct services use one-hour bounds.
- 2026-10-30: R2 verified including backend-down isolation and recovered RA74 root SSH. Initial8-resolver payload pool passed16/16 requests. Google/Yandex topologies selected for finite balanced matrix; no permanent channel or production routing change.
- 2026-10-30: first two full-workload screens completed8/8 windows without failure; no credible repeatable DL winner yet. Scoped restoration script prepared at DE`/run/slipstream-mp-bench/restore.sh`; it checks protected originals, removes only owned override/rule/services/binary, rechecks production WARP and compares stateless runtime rules. It has not run yet.
- 2026-10-30: all three56/64 topology screens completed12/12 full windows; no repeatable DL winner at that aggregate budget. Fastest complete Google pair proceeds to the predeclared matched112/224 concurrency screens, not a native scheduler change.
- 2026-10-30: Google112/224 recorded6 bootstrap failures with healthy DE/ordinary DNS controls; original failures retained. Working-provider continuation passed8/8 windows without a credible MP winner; no confirmation or deployment. R3 resolved as bounded negative experiment, not a speedup.
- 2026-10-30: R4 restored byte-identical originals/stateless rules and original WARP token path, rejected retired token, removed temporary runtime/credentials, and re-proved recovered RA74 root SSH. Aggregate archives collected and verified; only final repository closure remains.
- 2026-10-30: closure passed: both repositories'`git diff --check`, explicit new-goal whitespace/outcome check, prescribed operations secret scan and final diff/scope review. Owenclave unchanged; no commits/pushes. Execution is terminal.

## Completion

- Resolved outcomes: R1–R4 verified; requested first experiment completed, repeatable multipath speedup NOT established.
- Commands/artifacts: Python14 tests plus final targeted3; Meson6/6 (runtime16); FlowRelay race/vet/build; both runtime config checks, systemd/nft verification; external direct/production WARP TCP/UDP and fail-closed probes; exact archive manifests and restored-file/stateless-rule comparisons; final strict-pinned RA74 root SSH.
- Constraint/scope check: only four C benchmark/test/README files and this goal; pre-existing derived picoquic state retained; no production routing/DNS/credentials/Slipstream binary change, native scheduler change, new dependency, permanent service, application traffic logging, commit or push. Temporary direct channel intentionally removed.
- Final status: complete. Required documentation/secret/diff gates passed; all outcomes resolved, original runtime restored, no permanent channel deployed. Negative throughput result and unmeasured Google224 carrier limit remain explicit.
