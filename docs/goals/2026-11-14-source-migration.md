# Goal: standalone Slipstream mobile source snapshot

Status: unmet
Source: approved migration instruction, 2026-11-14
Last updated: 2026-11-14

## Objective
Create a safe, locally committed root monorepo at `/home/stfu/ai/dont/slipstream-mobile` from the effective Owenclave, C and Rust working trees.

## Execution Directive
Complete the frozen Required Outcomes using the listed Change Envelope and Primary Evidence. Work on the smallest unresolved outcome. Do not add requirements from reviews, tests, tools, speculative risks, or optional source text. Finish when every required outcome is resolved and affected constraints remain satisfied.

## Frozen Contract
- R1: Preserve effective sources, distinct vendored dependencies, docs and approved aggregate evidence; omit secrets/generated artifacts and nested Git metadata. Source: approved plan. Acceptance: manifest comparison and safe-tree scan. Status: verified. Evidence: 1409 imported safe source files accounted for; eight original repository status/diff fingerprints unchanged. Exact omission list in `../source-import.json`; both RU archive internal manifests verified and all eight evidence files copied with SHA256 identity.
- R2: Android builds sibling Rust non-destructively; C benchmark current path works; vendored patch handling and external signing are usable. Source: approved plan. Acceptance: focused checks and available build gates, exact unavailable-toolchain blockers recorded. Status: verified. Evidence: focused gates and explicit Android prerequisites below; independent review accepted final signing/build-cache corrections.
- R3: Root/scoped guides, architecture, provenance and operations boundary retain active/blocked work and historical experiment semantics. Source: approved plan. Acceptance: link/content review. Status: verified. Evidence: root/scoped guides, source provenance and historical evidence navigation present; original Owenclave goal files byte-preserved.
- R4: Only DE backend project MCP configuration, external credentials and host verification. Source: user addition. Acceptance: config check and read-only host identity. Status: verified. Evidence: parsed project config contains only `ssh-n-de1` and its server object exactly matches operations source; OpenCode debug config discovers it. After project initialization, `opencode mcp list` in the target reports `ssh-n-de1` connected. Matching SSH MCP read-only `hostname` returned `n-de1`; no remote writes.
- R5: Independent Review-Sol-Global review, source conservation, safe tracked tree and local multiline commit on main, clean status. Source: approved plan. Acceptance: review and Git verification. Status: verified. Evidence: Review-Sol-Global returned no blocking findings on final source; independent accounting of 1409 safe source files/11 adaptations and eight conserved source repositories. Fresh staged export contained 1434 byte-matching tracked files, no nested Git metadata, valid patch verification and working root operational links. Authorized multiline snapshot commit created on main; working tree clean and `git diff --check` passed. This final closure record is included by amending that local snapshot only.

## Constraints and change envelope
Target only; source trees remain unchanged. No remote writes, deployments, push, new remote, history import, topology duplication or CI activation. FlowRelay remains only under Owenclave. No keys, credential stores, private configs, build/cache/APK/AAR/SO artifacts. RA74 shares the DE backend; connection availability grants no deployment authorization.

## Current checkpoint
Terminal: repository deliverables verified and locally committed. No further source implementation is required. The no-secret-output constraint was violated by the verification incident below, so the contract is marked unmet rather than claiming an unqualified complete migration.

## Verification evidence
- `python3 -B -m unittest discover -s slipstream-c/bench -p 'test*.py' -v`: 14 passed.
- `meson setup build-candidate && meson compile -C build-candidate && meson test -C build-candidate --print-errorlogs` in C: build succeeded; 6/6 suites passed, including runtime17 tests.
- `go test ./...` in Owenclave FlowRelay: passed.
- `cargo test -p slipstream-dns --locked`: 12 tests passed; `cargo fmt --check` passed after formatting the two changed diagnostics.
- `bash -n owenclave/bin/lib/slipstream/build.sh`: passed. Instrumented cargo-boundary execution selected default sibling Rust and preserved all source hashes; compiler was deliberately replaced/stopped, so this is not Android compilation evidence.
- Gradle configuration assertions with `help`: absent external key selects unsigned release; synthetic external path selects that path with v3 signing. Both passed even with legacy `KEYSTORE_PASS` set; the removed bundled-keystore helper no longer creates a duplicate signing config.
- The original Android CMake cache points to the old cached source; new default build output is per-checkout ignored `.cache/slipstream-android/`, avoiding reuse of that incompatible binding without deleting old cache/source.
- Actual Android carrier attempt with the installed NDK r28c stopped at the retained NDK29.0.14206865 version gate. Rust1.97.1 and cargo-ndk4.1.2 are installed; NDK29 is absent. System Perl also lacks `FindBin` (detected while exercising preflight). Smallest unlock: provision the documented pinned Android environment plus Perl modules; do not relax pins.
- Actual `ANDROID_HOME=/home/stfu/Android/Sdk ./gradlew :app:compileOssReleaseKotlin --no-daemon` reached Kotlin but failed on missing generated `libexclavecore` bindings/AAR; upstream `./run lib core` must generate them in a complete Android toolchain. No imported AAR/SO, APK or device qualification is claimed. Initial offline run lacked Gradle dependencies; online resolution succeeded, so that is no longer the blocker.
- Source-to-target migration delta whitespace checks passed. Full initial-snapshot `git diff --cached --check` reports inherited upstream whitespace; unchanged source was intentionally not mass-normalized. Final working-tree diff check will be recorded after the commit.

## Checkpoint history
- 2026-11-14: Empty target verified; contract frozen; effective source/import inventory and secret exclusions completed.
- 2026-11-14: Local build-path adaptations, docs and DE-only configuration completed; C/FlowRelay/Rust DNS gates passed; Android toolchain/generated-core blockers isolated. No source or remote changes.
- 2026-11-14: Independent review accepted final source/signing/cache fixes; fresh tracked export, source conservation, local main commit and clean diff verified.

## Verification incident
One tool read of the generated native Meson test log matched its inherited-environment line and exposed credential values in tool output. Values are not repeated or retained in this repository. The generated native build/log directory was removed; it had never been staged or committed. Original sources were untouched. Output already emitted cannot be retracted by repository changes; affected credentials require operator rotation outside this migration's authorization. This is an actual violation of the explicit no-secret-output constraint, not a hypothetical risk or a reason to claim the code checks failed.

## Completion
- Repository outcomes R1–R5 verified; local snapshot only, no push/new remote/deployment or service changes.
- Android compile/APK/device qualification remains unavailable for the documented prerequisites; no unsupported success is claimed.
- No secrets/private keys/generated builds in the tracked snapshot; original dirty source diffs conserved.
- Final contract status: unmet solely for the irreversible tool-output confidentiality incident. Artifact migration itself is delivered.
