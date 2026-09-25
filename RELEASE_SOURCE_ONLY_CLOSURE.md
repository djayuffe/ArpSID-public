# pass380 / v970 test build-graph closure

The current source package is `0.0.690-pass380-v970-test-build-graph-closure`.
It compiles the heavyweight forensic patch bank once into a static library
(arpsid_forensic_patchbank) that 14 test targets link instead of each
recompiling the source — cutting duplicate compilation and peak build memory
without weakening isolation, and changing no shipped binary. New test:
`BuildGraphForensicPatchbankLibV970Tests`. AU/Logic, VST3 SDK,
signing/notarization, and installer validation remain external sign-off work.

# pass380 / v969 test-suite integrity closure (historical)

The v969 source package was `0.0.690-pass380-v969-test-suite-integrity-closure`.
It is a test-infrastructure audit-and-repair pass (no production/runtime change):
an audit of all 491 test sources found nothing genuinely legacy/obsolete to
remove; it restored the one true orphan (auv3_render_scratch_transport_v591) to
the build with a linkage-tolerant assertion, and de-flaked
ScopeTripleBufferMultiConsumerV687Tests. New test: `TestSuiteIntegrityV969Tests`.
AU/Logic, VST3 SDK, signing/notarization, and installer validation remain
external sign-off work.

# pass380 / v968 DrSID kit-save normalization + full-suite green closure (historical)

The v968 source package was `0.0.690-pass380-v968-drsid-kit-save-and-queue-test-closure`.
It normalized DrSID user-kit saves into DrSID mode (both `.arpsid` and `.json`)
and repaired four pre-v965 tests (V404/V408/V909/V590) that pinned superseded
PIPE-001/PIPE-013 and telemetry contracts. Test: `DrSidUserKitSaveModeNormalizationV968Tests`.

# pass380 / v967 DrSID quick-kit base-profile parity closure (historical)

The v967 source package was `0.0.690-pass380-v967-drsid-quick-kit-base-parity-closure`.
It re-syncs the on-screen DrSID quick-kit "Standard" base voicing to the
canonical factory DrSID base (the v909 tom-decay correction the GUI table
missed), makes the GUI-only "Standard" kit actually apply its profile instead
of being a silent no-op, and pins both with `DrSidQuickKitBaseProfileParityV967Tests`.
Curated quick kits are unchanged. AU/Logic, VST3 SDK, signing/notarization, and
installer validation remain external sign-off work.

# pass380 / v966 host parameter presentation authority closure (historical)

The v966 source package was `0.0.690-pass380-v966-parameter-presentation-authority-closure`.
It adds one shared parameter-ID-aware host presentation service so AUv2, AUv3
and VST3 display and parse parameter text through the canonical DSP laws,
real bounded UTF-8 ⇄ UTF-16 conversion in the VST3 controller, and gated VST
editor diagnostics. Regression test: `ParameterPresentationAuthorityV966Tests`.
AU/Logic, VST3 SDK, signing/notarization, and installer validation remain
external sign-off work.

# pass380 / v952 runtime behavior/parity closure (historical)

The v952-era source package was `0.0.690-pass380-v952` lineage.
It adds executable SID808 transport/reset audio coverage, parameter-specific
normalization/cardinality, generation-ordered dirty-fallback side-effect parity,
state-root adapter-policy reconciliation, and warning-clean production loop
bounds. Local source build and CTest are green (465/465). AU/Logic, VST3 SDK,
signing/notarization, and installer validation remain external sign-off work.

# v950 SEQ DrSID/SID808 reset preservation closure

- DrSID/SID808 structural reset now preserves owned SeqEnable and sequencer pattern state instead of killing drum transport.
- GUI DrSID mode selection, AU2/AU3 flavor policies, and file-bank persistence preserve restored SeqEnable for drum sequencer authority.
- Phase2 SEQ disable now releases DrSID under DrSID authority instead of BitPerfect.


## pass372 / fix-order #50 — DrSID user-kit save weak continuation

- `_drumSaveUserKit:` now weak-loads its delayed main-queue publish continuation before updating `_loadedDrumUserKitURL`, reloading the user-kit library, or writing bank status.
- Added `DrumUserKitSaveWeakContinuationV796Tests`.
- Version: `0.0.690-pass372-rt-safety-audit-drum-kit-save-weak-continuation`.

# ArpSID 0.0.690 pass358 — source-only closure carried forward

Source-only closure remains valid after fix-order #36 bank worker URL snapshot hardening.

# ArpSID 0.0.690 pass344 — source-only closure + build.sh

This package closes the source-only realtime-safety audit handoff that started from pass333 and continued through pass341.

## Closed in source

- Fix-order #1–#11 are carried forward from pass333.
- Fix-order #12: VST Cocoa factory preset range uses canonical 180 slots.
- Fix-order #13: DrSID legacy/canonical context policy split is documented and guarded.
- Fix-order #14: SID-core mutable surface is restricted to explicit internal/non-realtime accessors.
- Fix-order #15: wrapper-owned state-apply policy moved to canonical core.
- Fix-order #16: DIGI model/sample-bank public protocol is atomic only.
- Fix-order #17: parameter dirty-flush fallback telemetry distinguishes timing-loss from value-loss.
- Fix-order #18: AUv2 ramp-anchor scheduled/drop telemetry is explicit.
- Fix-order #19: stale post-#18 deferred-status wording is closed.
- Fix-order #20/#21/#22: source-only closure, root `build.sh`, and restored release-mode build helper contracts are closed.

## Validation performed in this sandbox

The Linux/source-only environment can configure and run source/unit guards. It cannot install AUv2 components, run Apple `auval`, launch Logic, notarize, or compile VST3 without the VST3 SDK.

Targeted source guards through `SourceOnlyClosureV767Tests` pass in this package.

## Required macOS release validation outside this sandbox

On the Mac, unpack this zip and run the normal release flow:

```bash
cmake -S . -B build -DARPSID_BUILD_TESTS=ON -DARPSID_BUILD_AUV2=ON
cmake --build build --target ArpSIDAUv2 --config Release
ctest --test-dir build --output-on-failure
# then install the AUv2 component using the project install script / documented release flow
auval -v aumu ArpS ASID
```

Expected closure result after macOS validation: AUv2 rebuilt from pass344, installed, `auval` passes, and Logic is relaunched so it loads the new component.

## Honest caveat

No AUv2/Logic runtime validation is claimed for this sandbox-generated zip. This package is a complete source-level closure artifact plus macOS validation handoff.


## pass344 build helper addition

The source zip now includes executable `build.sh` at the release root. Default usage (`./build.sh`) configures, builds, and runs CTest in `./build`. macOS AUv2 installation remains explicit opt-in via `./build.sh --install-auv2`; this sandbox did not run AUv2 install, `auval`, Logic, VST3 SDK builds, signing, or notarization.


## pass344 — build.sh release-mode closure

- Closed the regression reported after pass343 full CTest: `FullCTestPreflightScriptV670Tests`, `ReleaseClosureV701Tests`, and `ReleasePackagingV702Tests` failed because the new root `build.sh` omitted the older release-check/package-release contract surface.
- `build.sh` now supports `--release-check` and `--package-release`; package-release forces release-check first and then delegates to `scripts/package_release.sh`.
- Added `RootBuildScriptReleaseModesV769Tests`; isolated validation of the three previously failing tests plus v769 is green.


## pass347 macOS install handoff update

A user-provided macOS log confirms that the pass344/pass347 source lineage completed full CTest (`313/313`), AUv2 build/install, signature replacement, and AU cache fast-refresh. `auval` and Logic validation are still pending and are not claimed by this package.


Pass346 adds `RootBuildScriptAuvalModeV771Tests` and `./build.sh --validate-auv2` for executable AUv2 auval handoff.

## pass347 macOS closure command update

Pass347 adds `./build.sh --macos-closure`, a macOS-only final validation helper. It runs release-check, full CTest, AUv2 build/install, AU cache refresh, and strict targeted auval in one explicit command. This source package still does not claim auval or Logic runtime success until those external logs are captured.


## pass349 note

For final Mac validation, prefer `./build.sh --macos-closure --closure-log-dir ./release-logs` and provide the generated timestamped log.

## pass363 / fix-order #41 — DIGI record auto-stop weak continuations

- Fixed DIGI record auto-stop continuations queued from AudioQueue and AVAudioEngine callbacks so they no longer retain callback-local controller references into the main queue.
- Queued stops now use `stopWeakSelf`, strong-load on main, and compare `_digiRecordGeneration_v184_` against the callback/session generation before stopping.
- Guard: `DigiRecordAutostopWeakContinuationV787Tests`.
## v948 dirty flush staging authority closure

- AU3 async/UI param ingress now sanitizes with param-specific sanitize/default instead of generic clamp.
- AU3 dirty-flush fallback now synchronizes params_, renderParams_, and runtimeModel_.stateRoot() before backend reprojection.
- Added AuthorityDirtyFlushStagingV948Tests.
