# Goal: Test DNS Domain Sharding

Status: complete
Source: User request after adding `tt.x.ass-peak.de` NS delegation, 2026-09-10.
Last updated: 2026-09-10

## Objective

Document observed delegation, implement an opt-in per-path DNS suffix experiment,
and deliver an actual Android LTE tested acceptance or supported rejection without
changing the accepted APK unless the native gain survives the real Kotlin/gVisor gate.

## Execution Directive

Complete the frozen Required Outcomes using the listed Change Envelope and Primary
Evidence. Work on the smallest unresolved outcome. Do not add requirements from
reviews, tests, tools, speculative risks, or optional source text. Finish when every
required outcome is resolved and affected constraints remain satisfied.

## Frozen Contract

- R1: Document the second delegated subdomain using authoritative observations.
  Source: user requires both authoritative NS verification first.
  Acceptance: DNS readiness distinguished from runtime/client/performance support.
  Primary evidence: bounded nonrecursive queries to both Cloudflare NS; operations docs.
  Status: verified.
  Evidence: both return NOERROR referral `tt.x.ass-peak.de 300 NS x.ass-peak.de`
  and additional `x.ass-peak.de 300 A 195.128.101.186`; existing `t.x` also observed.
- R2: Implement and locally verify explicit resolver/domain mapping in one QUIC child.
  Source: user requests multi-domain experiment preserving accepted safety semantics.
  Acceptance: data and polls use the assigned suffix, bounded DNS capacity, pinning,
  bootstrap/path recovery and independent streams preserved; invalid mappings fail.
  Primary evidence: isolated source patch, codec/unit and local fault tests.
  Status: verified.
  Evidence: resumed existing H14 Android ELF c8c3283899ad32a4e99559f8e0e1c9d22414731eee1fbeca69c3c853c9feb3a0;
  existing host test executable passes26 tests, DNS7 tests, real QUIC5 fault modes.
  Same accepted picoquic sender; both suffixes carry data and polls in phone aggregates.
- R3: Enable and verify both suffixes on the owning server without route changes.
  Source: user authorizes second domain, not fallback or weakened production security.
  Acceptance: existing/new suffix pinned payloads work; common private WARP backend,
  certificate, limits and zero retained traffic logs unchanged; documented rollback.
  Primary evidence: remote backup/config validation, service state and payload probes.
  Status: verified.
  Evidence: live unit already contains both domains; no redeployment on resume.
  Android old/old and old/new exact payloads pass; full DER pin matches server.
- R4: Run calibrated Android LTE comparison and deliver tested decision.
  Source: actual LTE smoke, balanced controls, modest consistent gains count.
  Acceptance: same resolver/resource/workload controls, both suffixes demonstrably used;
  investigate initial losses; six fresh confirmation pairs if plausible screening gain.
  Any promotion requires opt-in APK integration and actual Kotlin/gVisor repeat.
  Primary evidence: immutable aggregate JSONL, balanced report, source/artifact hashes.
  Status: verified.
  Evidence: six-pair resumed confirmation and separate reversed-assignment CTTC
  reject promotion: no repeatable short-upload gain with acceptable reliability.
  Original failures/interrupted windows retained; no Kotlin/APK integration warranted.
- R5: Restore temporary facilities and document results without altering accepted delivery.
  Source: user cleanup, production safety, every attempt documented, no push.
  Acceptance: no temporary listeners/keys/rules/jobs; VPN off, zero children, accepted
  profile restored; retained second domain only if validated; verified changes committed.
  Primary evidence: teardown/service/phone checks, docs and commit IDs.
  Status: verified.
  Evidence: fixture/rule/key cleanup, final server/phone and diff/secret checks passed.
  Operations commit `ddcc73a`; Owenclave changes are in the commit containing this goal.

### Constraints And Non-goals

- Preserve full DER pin and handshake signature verification, stdin secrets, gVisor,
  independent QUIC streams, one child, aggregate 56 workers / 64 queued queries.
- Preserve bounded bootstrap rotation and server path-ID/CID fix; no blind UDP fallback.
- No credentials, payload/QNAME captures, full configs or persistent traffic logs.
- No DNS API writes, extra public IP requirement, production direct fallback, host WARP,
  unrelated optimization or changes to `/home/stfu/Torrents/apk`.
- `tt.x` is one character longer: distinguish capacity cost from domain-keyed limits.
- Native Python adapters are not Kotlin equivalence. Historical H13 baseline failures
  remain unresolved separately; this experiment cannot claim universal payload success.

## Change Envelope

- Isolated native source under ignored `build/dns-smoke/`, retained experimental patch
  and focused tests under `bin/lib/slipstream/`; smoke host/device/report harness.
- This goal, DNS contract if accepted, operations README / n-de1 and affected invariant.
- Remote `/etc/systemd/system/slipstream.service`: repeated `--domain` only after tests,
  timestamped backup and announced short restart; existing private backend unchanged.
- Temporary authenticated fixture and optional explicitly isolated direct test backend
  only if needed, with existing safe provisioning/teardown boundaries.
- Kotlin/native production integration only after credible native performance evidence.

## Current Checkpoint

- Closed R1-R5: supported rejection, verified cleanup and committed evidence, no push.
- No remaining H14 action, new sweep, APK build, install or promotion.

## Current State

- Starting operations HEAD `10be80a`; Owenclave HEAD `b231089`. Existing uncommitted
  H14 changes were preserved throughout resume; no reset or duplicate campaign.
- Accepted APK `build/dns-multipath/fixed.apk` SHA-256
  `c8f2c1943dfd75dc40d35e5243942e5a302c5a6ae7907ce2447631a627ca36ac` unchanged.
- DNS and server both ready. Live Slipstream PID3189120, Flowd3142713, WARP589;
  NRestarts0/0/0. Resume did not restart any production service.
- Backup `/root/backups/owenclave-h14-20260909T215102Z/` holds original single-domain
  unit and nftables.conf. Resume added `runtime-resume.nft` before temporary firewall edit.
- Temporary `/run/owenclave-h14`, transient `owenclave-h14-fixture.service`, TCP40004,
  rule `owenclave-h14-fixture`, private key/credentials and host/phone copies removed.
  Initial exact WARP trace IP allowlist timed out; historical H11/H13 range104.28.0.0/16
  on this port only passed three exact4096-byte interface-bound WARP probes,
  fingerprint34ffae51947d. Neither failed probe nor completed windows were overwritten.
- Deliberate deviation from prior direct preference: both arms use unchanged production
  WARP to avoid Flowd's dependent Slipstream restart. This retains WARP as a confound.
- Final phone check07:17Z: accepted hash verified, LTE/LTE, Wi-Fi/VPN off, children0,
  battery100%,28C; production manual multipath inspected unchanged after restoration.
  Direct ya.ru302/google301: ordinary LTE evidence, not Restricted LTE acceptance.
- Host runner is ignored `build/dns-smoke/h14-run.py`; all results exclusive-create.

## Checkpoint History

- 2026-09-10 R1: verified both authoritative delegations and documented DNS-only state.
- Resume: exec-raw transfer now succeeds after reported provider unblock. Existing
  remote fixture.py was absent; no late partial file overwritten. No production rewrite.
- Read-only checks: wrong guessed nft table `inet filter` absent, actual `inet firewall`;
  public HTTPS by IP rejects SNI, bpftrace absent. These are not carrier test failures.
- Existing native unit binary26/26 and DNS7/7 pass. Cargo invocation failed on root-owned
  `.cargo-build-lock`; direct existing test executable used without ownership/reset changes.
  One normal binary rejected `--list`; correct test binary identified. Smoke15/15 pass.
  Existing QUIC fault harness rerun because prior results were not retained in goal:
  reorder/duplicate, primary loss, dead primary each131072 exact bidirectional bytes,
  half-close; both-dead/wrong-pin exit1, no payload. Pin DER SHA256
  `0f7e654ee970baee94bddc7149a7b32e7b850a47baae2186ba54efcb56e9a56b` matches DE/app.
- `h14-screen`: p1 C/T passed, p2 T retained one payload failure, p2 C interrupted
  after two completed short uploads and lost ADB, no cleanup/window/exit evidence.
  Report2 correctly rejects incomplete series; zero-byte report artifact retained.
  Device later returned with children0/VPNoff. No failed baseline was excluded as success.
- `h14-resume-screen`: fresh CTTC completed, C16/16 upload ACK, T15/16.
  Paired medians seq8 +24.33%, seq32 -0.67%, loaded8 +5.00%, loaded32 +1.83%,
  DL -5.17%; candidate seq512 failure. Modest short-upload signal justifies six fresh
  confirmations, not promotion. First series and interruption stay in the history.
- Second resume 06:41Z: no host benchmark process or held smoke lock; phone children0,
  Wi-Fi/VPNoff, LTE/LTE,100%,27C, accepted APK hash unchanged. Direct ya.ru302/google301.
  Production PIDs/NRestarts unchanged; fixture active, three exact4096-byte probes
  explicitly bound to CloudflareWARP pass. No redeployment or production restart.
- `h14-confirm`: p1C/T,p2T/C,p3C/T,p4T complete. p4C ended with host
  KeyboardInterrupt during loaded512, no cleanup/window/host_exit. Original retained.
  One fresh `h14-confirm-p4-control-resumed.jsonl` replaces it only in the explicitly
  labeled resumed analysis; p5C/T and p6T/C run individually, exclusively, to completion.
  Pair4 has a time gap; it is not represented as an uninterrupted adjacent pair.
- `h14-confirm-resumed-report.json`: C44/48 versus T43/48 exact upload ACKs,
  failed windows4/6 versus5/6; all12 startups and cleanup pass. C failures: loaded32
  ACK(p1), loaded512 ACK(p2), loaded128 TLS timeout(p4 replacement), loaded128 ACK(p5).
  T failures: loaded128 ACK(p1,p2), loaded512 ACK(p4,p5,p6), recovery TLS reset(p6).
  All ACK timeouts40s; recovery reset11.377s. Failed local submission is not delivery.
  Medians seq8 +24.08%(3/6), seq32 -3.54%(3/6), loaded8 +0.69%(3/6), loaded32
  +14.57%(3/5 defined); DL -5.00%(2/6). All short/DL bootstrap intervals cross zero.
  Sequential512 +13.23% does not outweigh loaded512 -100%(0/5 defined).
- `h14-confirm-without-p4-report.json` is sensitivity, not six balanced confirmations:
  seq8 +50.17%,seq32 +4.54%,loaded8 +5.00%,loaded32 +20.22%,DL -0.89%; short
  intervals still cross zero; C37/40 versus T36/40 upload ACK plus T recovery failure.
  Both reports contain input filenames/hashes; original partial p4 remains evidence.
- Source diagnosis: `compute_mtu` gives140 for suffix lengths15 and16, so this pair
  does not change the integer QUIC MTU despite a longer QNAME. Both suffixes carry
  data/polls, no bootstrap failures. Failures are not a dead fixture. WARP and prior
  native load-competition/TLS failure classes remain confounds; no exclusive blame.
  Reverse assignment is a distinct causal check, not repeating the same screen.
- `h14-reverse-screen-report.json`: distinct CTTC assigns `tt.x` to Safe .88 instead
  of Basic .1, same ELF, aggregate56/64, workload and WARP. C15/16 versus T14/16
  upload ACK; failed windows1/2 versus2/2, all startups/cleanup pass. C seq128 ACK
  IncompleteReadError5.752s; T loaded128 ACK40.002s(p1), loaded512 ACK40.002s(p2).
  Paired medians seq8 -5.21%(1/2 wins), seq32 +0.75%(1/2), loaded8 -5.11%(0/2),
  loaded32 -22.82%(0/2), DL -12.97%(0/2), recovery -36.53%(0/2).
  Reversal does not rescue the short-upload or reliability result. H14 rejected for
  APK promotion, not proof that all possible domain sharding is ineffective.
- Accepted APK production gVisor checks `h14-production-{single,automatic,multipath}`
  retain23/24 exact upload ACK. Single has7/8: loaded128 ACK timeout40.001s;
  all sequential sizes, other loaded sizes, DL1MiB and recovery4096 pass. Automatic
  and multipath each8/8 plus DL/recovery pass. Single background TLS reset12.034s
  is separately retained, not counted as its foreground failure. All three independent
  HTTPS probes return200, DE WARP-on, and transaction-checked STUN succeeds.
  Latest MP STUN fingerprintbdc05888361f matches three remote probes explicitly
  bound to CloudflareWARP. Single/automatic fingerprints differ, consistent with
  WARP per-flow/destination variation; not an assertion that every WARP IP is fixed.
  This is ordinary LTE, not Restricted LTE or real Telegram acceptance. Existing
  intermittent payload failures remain unresolved in the broader throughput goal.
- Teardown: stopped only fixture, removed exactly its runtime rule (handle59), then
  removed owned runtime contents. First deletion guard rejected unexpected shutdown
  `stats.json`; follow-up absence assertion also failed. Inspected it before allowing
  deletion, rather than deleting an unknown file blindly. Aggregate counters preserved:
  TLS-complete580, authenticated579, upload-complete212, reply-drained566,
  HTTP-timeout14, I/O-error0, close-abort353. These span the whole fixture campaign
  and probes, not individual paired windows; not all failures are lost ready ACKs.
- Final remote checks: Slipstream3189120, Flowd3142713, WARP589 active, NRestarts0/0/0,
  unchanged through resume. Slipstream/Flowd null sinks, WARP's preexisting unit
  journal/inherit unchanged; no traffic logging enabled. Both suffix flags remain.
  Public UDP195.128.101.186:53 and private warpns10.200.0.2:40001 present; temporary
  40003/40004/40005, retired40002/9100 and private SSH41924 absent. Fixture unit
  not-found; runtime directory/rule absent; persistent nftables byte-identical backup.
  `systemd-analyze verify` for Slipstream/Flowd and `nft --check` pass. MasterDNS/flowtls
  inactive/disabled, host-wide WARP and Node Exporter masked/inactive.
- Experimental patch retained as `bin/lib/slipstream/multidomain-experimental.patch`,
  not in `build.sh`. Forward apply-check against accepted cache and reverse apply-check
  against tested H14 source pass; sender and accepted bootstrap/path-ID safety preserved.
  Added CLI/layout-manifest tests: smoke17/17 pass. No native or APK rebuild was needed
  after the tested source was captured verbatim. Existing broader lint2371 errors/27
  hints and Clippy warnings remain red baseline, not suppressed or represented as green.
  Initial staged diff-check caught whitespace-only patch context lines missed while
  untracked; normalized empty context lines, then both apply-check directions pass again.
- Final verification: protected host credential files and empty directory removed;
  Termux carrier/cert/script files and empty smoke directory removed. No benchmark
  remains. Final UI inspection confirms manual multipath, no profile edit, children0.
  One attempted lock-wrapped UI check was denied by the tool's local path rule; no
  permission change or bypass. Read-only UI inspection then passed without lock access.

## Completion

- R1-R5 verified with a supported rejection. Operations commit `ddcc73a`; Owenclave
  commit retains the unapplied experiment, harness/tests and this completed goal.
- Main evidence under ignored `build/dns-smoke/`: `h14-confirm-resumed-report.json`,
  `h14-confirm-without-p4-report.json`, `h14-reverse-screen-report.json`, all original
  JSONL including interrupted/failed windows and production `*-egress.jsonl`.
- Accepted APK remains unchanged; experimental source retained, validated dual-domain
  server retained, all temporary facilities removed. Broader multipath-throughput goal
  remains separate and active; this rejection does not close its payload-failure gate.
- Closure checks: smoke17/17, experimental patch forward/reverse apply-check, final
  phone/server checks, both repositories' `git diff --check`, operations secret scan
  and changed Owenclave file secret scan pass. Native26/DNS7/QUIC5 evidence remains
  applicable to the unchanged tested source; no Kotlin or production build diff.
- Constraint/scope check: one phone owner, exclusive windows and failures retained,
  no secret/payload/QNAME output, no routing/security relaxation, no production restart
  on resume, no APK promotion, no push. Final status: complete.
