# v950 SEQ DrSID/SID808 reset preservation closure

- DrSID/SID808 structural reset now preserves owned SeqEnable and sequencer pattern state instead of killing drum transport.
- GUI DrSID mode selection, AU2/AU3 flavor policies, and file-bank persistence preserve restored SeqEnable for drum sequencer authority.
- Phase2 SEQ disable now releases DrSID under DrSID authority instead of BitPerfect.

# Current status — ArpSID 0.0.690 pass380

Latest source head: pass380 / v871 - closes the remaining v870 audit ledger across C64 render rollback, SynthMode identity, and all-slot SID808 factory musical shape. Fixes: C64 render mutation now snapshots and rolls back the PHI2 machine, runtime SID sink, C64 SID bridge, and platform mutation journal as one transaction; dirty-log wrap during an active render transaction defers full 64 KB platform RAM sync until commit and is consumed/cleared on rollback; C64 play-call cap hits and skipped due calls now publish direct telemetry; platform rollback proof failures are counted; canonical `SidTimedEvent` and AU `TimedEvent` carry `voiceToken`; held replay stamps note-on and note-off with the original token; SynthMode scheduler/allocator can bind explicit tokens; NoteOff prefers canonical voice token before channel/note/noteId lookup; SID808 factory slots 120..149 now guard Kick/Tom triangle/pulse-width-zero staged sources, OpenHat ring, Clap burst/tail behavior, Cowbell ringing without late bloom, and per-hit waveform overrides remain valid. Guards: `C64RenderTransactionV871Tests`, `SynthModeVoiceTokenV871Tests`, `Sid808FactoryMusicalShapeV871Tests`, updated `RenderBridgeSnapshotStackGuardV806Tests`, updated `RealtimeSourceLintV817Tests`, retained `Sid808MusicalShapeV870Tests`, `Sid808HitOverrideV626Tests`, and `KitSid808FactoryVoicePrecedenceV637Tests`; full CTest 394/394 PASS; AUv2 install/cache refresh/strict validation PASS; `auval -a` lists `ArIn`, `ArpS`, `C64P`, `DrSD`, `S808`; installed AUv2 SHA256 `2249ae85f5610e57e359951719bae609bc589fe060b9b57adbe4530027250da0`. Detailed docs: `C64_SID808_SYNTHMODE_CLOSURE_V871.md`, `RELEASE_NOTES_V871.md`.

Previous source head: pass380 / v870 - closes the remaining SID808 musical-shape ledger after v869. Fixes: `Sid808Engine` now has a bounded four-stage per-voice microprogram; Kick starts high and falls through punch/body/tail stages without extra gate clicks; Tom starts above the body pitch and drops through body/tail stages; OpenHat stays noise-based but runs a longer high-pass shaped ring program; Clap runs scheduled gated noise bursts plus a lower tail; Cowbell alternates pulse partial frequencies/pulse widths before a band-pass shaped ringing tail; SID808 `allNotesOff()` force-idles the three SID voices and clears filter routing so long one-shot tails cannot contaminate later muted/setup boundaries; `SidRuntimeHostSurface` includes the standard size/integer headers directly. Guards: `Sid808MusicalShapeV870Tests`, retained `Sid808AudibleAuthorityV865Tests`, `Sid808SnareOneShotV867Tests`, `Sid808SnareCompleteClosureV868Tests`, `Sid808SnareBodyRoutingV869Tests`; full CTest 391/391 PASS; AUv2 install/cache refresh/strict validation PASS; explicit Logic/AU cache reset backup `~/Library/Caches/ArpSID-cleared-logic-au-cache-20260704-201718`; strict `auval` PASS for `ArIn`, `ArpS`, `C64P`, `DrSD`, `S808`; installed AUv2 SHA256 `6c8a5f336c7e73643b7037d8fc05755c76e1ad509897dcc1910dae0d902a7042`. Detailed docs: `SID808_MUSICAL_SHAPE_CLOSURE_V870.md`, `RELEASE_NOTES_V870.md`.

Previous source head: pass380 / v869 - closes the consolidated SynthMode/SID808 ledger after v868. Fixes: AU raw MIDI held replay now stores negative synthetic anonymous note IDs, so noteId-less NoteOff releases replayed SynthMode voices instead of missing positive serial-derived fake IDs; real positive host-noteId voices remain protected; direct SynthMode in AU and Phase2/VST now has a two-pass orphan reconciler with held-ledger cleanup and a VST held-key mirror; SID808 snare body micro-stage retriggers with a real gate edge and is guarded by rendered RMS/peak checks; SID808 Snare/Clap/Rim shared voice-1 filter routing is active-drum owned rather than factory-table inherited; SID808 telemetry exposes active voice count, below-audible active blocks, literal zero-peak active blocks, raw/post-DC peak and mean, DC-blocker health, and snare snap/body metrics; the AU authority label now shows `SILENT-ACTIVE` for active SID808 voices below the audible floor. Guards: `SynthModeHeldReplayIdentityV869Tests`, `Sid808SnareBodyRoutingV869Tests`, extended `Sid808AudibleAuthorityV865Tests`, retained `Sid808SnareCompleteClosureV868Tests`; full CTest 390/390 PASS; AUv2 install/cache refresh/strict validation PASS; explicit Logic/AU cache reset backup `~/Library/Caches/ArpSID-cleared-logic-au-cache-20260704-160646`; strict `auval` PASS for `ArIn`, `ArpS`, `C64P`, `DrSD`, `S808`; installed AUv2 SHA256 `6a57b3725c9e8fbe184c038bf6cb1f18e243d120fcba865dce9dd989254adc7e`. Detailed docs: `SYNTHMODE_SID808_CLOSURE_V869.md`, `RELEASE_NOTES_V869.md`.

Previous source head: pass380 / v868 - closes the remaining SID-808 snare audit after v867. Fixes: SID808 forced-voice note ownership now uses allocator-only bookkeeping so the default melodic voice is never briefly gated before drum programming; snare hits run a render-scheduled high-frequency noise snap followed by the authored body after about 7.5 ms; runtime snare config forces noise, zero sustain, and filter routing even for pulse-only sustained overrides; factory snares across slots 120..149 author the filter flag; and shared SID voice 1 routing is current-hit owned so Snare filter shaping does not dull Clap/Rim. Guards: `Sid808SnareCompleteClosureV868Tests`, `Sid808AudibleAuthorityV865Tests`, `Sid808SnareOneShotV867Tests`; full CTest 388/388 PASS; AUv2 install/cache refresh/strict validation PASS; installed AUv2 SHA256 `fc863eccdc85890b8dc896256a0febf7897f986975ae1fc3b591e640a6907304`. Detailed docs: `SID808_SNARE_COMPLETE_CLOSURE_V868.md`, `RELEASE_NOTES_V868.md`.

# ArpSID 0.0.690 pass380 — current source-release closure status

Latest installed/source head: pass380 / SID-808 v866 — closes the delayed scheduled-hit telemetry gap found after the v865 audible-authority release. Fixes: delayed `noteOnAt(offset > 0)` and `noteOnAtWithOverride(offset > 0)` now dispatch through `DrumEngineHostBridge::noteOn()` / `noteOnWithOverride()` instead of bypassing the bridge publisher with direct router calls; scheduled transport/sequencer hits therefore update routed-hit count, last routed drum/note/velocity, active SID-808 context, and bridge output peak just like immediate pad/MIDI hits. The scheduled queue contract is documented as render-thread sequencer ingress, not a cross-thread GUI/host producer queue. Guard: `Sid808AudibleAuthorityV865Tests` now covers delayed Kick, delayed override Clap, multiple scheduled notes in one block, block-end scheduled notes, and queue overflow counting; detailed docs: `SID808_SCHEDULED_TELEMETRY_CLOSURE_V866.md`, `RELEASE_NOTES_V866.md`. Validation completed locally: focused SID-808/transport/no-silence CTest set 8/8 PASS, AUv2 bundle build PASS, direct AUv2 component smoke PASS, direct AUv2 SID-808 component smoke PASS, direct AU3 SID-808 render-event smoke PASS, AUv2 install/cache refresh/strict validation PASS, codesign PASS. Installed AUv2 binary SHA256: `f55909a0dfe07b32816a97e2ea2d7fe2c9b5fc4d7be7b87aa9f1924a9d142744`.

Previous field candidate: pass380 / SID-808 v865 — closes the remaining SID-808 audible-authority and truth-in-telemetry surface. Fixes: SID-808 waveform config now accepts raw SID control waveform bits and internal waveform nibbles at kit/GUI/override/final-voice boundaries; every canonical drum family is guarded by an audible-minimum bridge render regression; `DrumEngineHostBridge` publishes configured kit, routed-hit count, last routed drum/note/velocity, active SID-808 context, and output peak; AU telemetry exposes SID-808 bridge fields and uses SID-808 engine levels when SID-808 owns audio; the AU HUD/panels now show `AUTH SID808-BRIDGE KIT### HIT# PK ## REPL/FAILOPEN/IDLE`. Guard: `Sid808AudibleAuthorityV865Tests`; detailed docs: `SID808_AUDIBLE_AUTHORITY_CLOSURE_V865.md`, `RELEASE_NOTES_V865.md`. Validation completed locally: focused SID-808/transport/no-silence CTest set 8/8 PASS, AUv2 bundle build PASS, direct AUv2 component smoke PASS, direct AUv2 SID-808 component smoke PASS, direct AU3 SID-808 render-event smoke PASS, AUv2 install/cache refresh/strict validation PASS, codesign PASS. Installed AUv2 binary SHA256: `8769abd235de63e86bace95a34f5651843f0edb65d385d432582553a2954bdbc`.

Previous field candidate: pass380 / SID-core v864 — closes the uploaded v860 SID-core audit surface across pulse-width edge cases, TEST/noise reset, hard-restart gate timing, sync/ring source timing, filter routing/cutoff law, `$D418` master-volume DC behavior, waveform-select compatibility, zero-cycle/subcycle timed-write rendering, and C64 readback parity. Fixes: calibrated DC is now volume-owned and pre-DC/RC, native interval helpers advance exact SID cycles/subcycles instead of host samples, native partial intervals no longer depend on the host-sample planner cursor, `SIDVoice::setWaveform()` accepts internal nibbles and raw SID waveform masks, and `SidReadbackModel` uses the same no-cascade sync law as `SIDChip`. Guards: `SidCoreAuditV864Tests` and `MultiSampleRateRenderFingerprintV529Tests`; detailed doc: `SID_CORE_AUDIT_CLOSURE_V864.md`. Full macOS closure passed in `release-logs/macos-closure-20260704-004703.log` with release-check 7/7, full CTest 384/384, strict installed AUv2 verification, and installed binary SHA256 `fe08fbbf74ac4db795b77db039f2b20428e3768fc44bffc4e3956cfd37489a72`.

Previous field candidate: pass380 / SID-808 v863 — canonical DrSID-mode drum MIDI now routes through the kernel target hook (`runtimeTriggerDrSidNote` / `runtimeReleaseDrSidNote`) before any canonical DrSID fallback, so SID-808 GUI pads, transport sequencer notes, and host MIDI reach the bridge-owned `drumEngineBridge_` audio path. The v861 fail-open output rule is retained: SID-808 bridge scratch replaces the final bus only when the bridge has renderable activity. Guards: `Sid808TargetRoutedDrumMidiV862Tests`, `DrumBridgeNoSilenceV613Tests`, `DigiMidiPadTransportStopV745Tests`, `Sid808RestorePreloadDrainV803Tests`, and `Sid808StrictAuSlotPolicyV861Tests`. Installed AUv2 validation passed for all five flavors; installed binary SHA256: `af2ea5def5d37e1ce6054aee12d99621203a0f6c31ccb7484a00507c3ec50af7`.

Previous field fix: pass380 / SIDPLAY v844 — C64P continuous-runtime catch-up still runs only the current AU buffer's PHI2 span, but CATCHUP MAX records true pre-cap debt, realtime backlog is dropped only when debt exceeds the current buffer span, bypassed synth/BPE/DrSID/ARP/DIGI note state is cleared in the C64 player branch, and open-bus light telemetry/scope visualization uses real decay/age/counter state. Guards: `C64RenderStallInstrumentationV840Tests`, `GuiTelemetryScopeClosureV743Tests`, `C64ScopeTelemetryFixClosureV744Tests`.

Previous field fix: pass380 / SIDPLAY v841 — C64P continuous-runtime catch-up is capped to the current AU buffer's PHI2 span, so stale passive-cycle debt is no longer compressed into the last audio sample. The user initially confirmed after install/cache refresh that SID playback was smooth in Logic; v844 supersedes it after a later report of remaining choppiness / possible stuck poly state.

Previous source-closure fix: pass380 / P0 render-apply RT-only closure — render-drained state restore now keeps Non-RT canonicalization/hydration on the producer side, applies an already-prepared root by swap, and uses RT-only hydrated parameter helpers on the audio thread. Guard: `RenderApplyRTOnlyHelpersV804Tests`.

Previous fix: fix-order #57 / P0 #4 — SID-808 deferred factory-slot load no longer waits for teardown/reset after render-drained restore. `schedulePendingStateRestore()` preloads matching SID-808 bridge kits on the non-RT producer side; render apply skips the queue if the preload matches and still has an RT-safe queue fail-safe. Guard: `Sid808RestorePreloadDrainV803Tests`.

# ArpSID 0.0.690 pass377 — final source closure validation

## pass377 / fix-order #55 — final source closure validation

- Closed the final self-staling guard risk from pass376: `StaleVersionGuardSweepV800Tests` now derives the current package pass from `VERSION.txt` and rejects stale `passNNN` literals dynamically instead of hard-coding the previous release number.
- Added `FinalSourceClosureV801Tests` plus `RELEASE_FINAL_SOURCE_CLOSURE.md` as the top-level final source-release boundary.
- Source-level P0/P1/P2 closure is guarded by `AbsoluteP0ClosureV797Tests`, `AbsoluteP1ClosureV798Tests`, `AbsoluteP2ClosureV799Tests`, and `FinalSourceClosureV801Tests`.
- External release validation is still intentionally separate: `auval`, Logic runtime, VST3 SDK/toolchain validation, and notarization remain pending until captured Mac logs prove them.

# ArpSID 0.0.690 pass376 — stale closure guard cleanup after absolute P2

## pass376 / fix-order #54 — stale closure guard cleanup after absolute P2

**Scope.** Fully validated the user-supplied pass343 closure list against the current pass375 package and found remaining stale pass359 assertions in closure/status guard tests.

**Fix.** `Post18ClosureStatusConsistencyV766Tests`, `SourceOnlyClosureV767Tests`, `Auv2SourceHygieneV733Tests`, `MacosInstallStatusHandoffV770Tests`, `RootBuildScriptAuvalModeV771Tests`, `RootBuildScriptMacosClosureV772Tests`, `RootBuildScriptMacosClosureLoggingV773Tests`, `MacosBuildGreenStatusV774Tests`, and `RootBuildScriptClosureLogCommandV775Tests` now derive the current pass from `VERSION.txt` and verify `version.h`/README/status dynamically. Added `StaleVersionGuardSweepV800Tests`.

**Result.** The pass343 closure claims remain carried forward and are validated in the current package without stale pass-number failures. External AUv2/Logic/VST3/notarization validation remains intentionally separate.

## pass374 / fix-order #52 — absolute P1 closure audit

- Version: `0.0.690-pass374-rt-safety-audit-absolute-p1-closure`.
- Re-audited all P1-labelled source/status surfaces in the current package. The original P1 matrix entries P1-01..P1-08 are closed/scoped-closed; late P1-13 and P1-20 source fixes are present; legacy P1 RT-safety/output-tap/physical-exactness notes remain represented.
- Added guard `AbsoluteP1ClosureV798Tests`, covering matrix closure status, required P1 guard registrations, VST Cocoa canonical 180-slot range, no-sink POTX/POTY PotXYApprox wiring, and strict-vs-physical-exactness status separation.
- This does not claim external AU validation; auval, Logic runtime, VST3 SDK/toolchain validation, and notarization remain external release steps.


## pass373 / fix-order #51 — absolute P0 closure audit

- Version: `0.0.690-pass373-rt-safety-audit-absolute-p0-closure`.
- Re-audited all P0-labelled status in the current source package. All known P0 items are represented as closed/fixed: pass685 P0-1/P0-2 capture safety, pass686 top-level critical P0 items 1..5, output-tap IOProc P0 closure, and v0.0.690 P0-1..P0-12.
- Added guard `AbsoluteP0ClosureV797Tests` to enforce required P0 closure markers and prevent P0 status from drifting back to pending/open/deferred language.
- This does not claim external AU validation; auval, Logic runtime, VST3 SDK/toolchain validation, and notarization remain external release steps.


## pass372 / fix-order #50 — DrSID user-kit save weak continuation

- `_drumSaveUserKit:` now weak-loads its delayed main-queue publish continuation before updating `_loadedDrumUserKitURL`, reloading the user-kit library, or writing bank status.
- Added `DrumUserKitSaveWeakContinuationV796Tests`.
- Version: `0.0.690-pass372-rt-safety-audit-drum-kit-save-weak-continuation`.

# ArpSID 0.0.690 pass369 — embedded VST focus weak continuation

## pass368 / fix-order #46 — embedded VST presentation weak continuation

This pass closes a remaining embedded VST editor-presentation lifetime surface. `prepareForEmbeddedVSTPresentation` queued a main-thread first-responder/fullscreen-preparation block that captured the view controller through `self`. Embedded VST hosts can attach/detach the Cocoa view while such a block is pending, so the continuation now captures `__weak ArpSIDViewController* weakSelf_v792`, strong-loads it on the main queue, bails if the editor is gone, and performs window/fullscreen/first-responder work only through the weak-loaded controller. Guard: `EmbeddedVSTPresentationWeakContinuationV792Tests`.

# ArpSID 0.0.690 pass361 — bank/preset panel completion URL snapshots

- Version: `0.0.690-pass361-rt-safety-audit-bank-panel-url-snapshots`
- Latest closed fix: fix-order #39, bank/preset AppKit completion handlers now snapshot `NSURL`/filename/stem/path values before IO/publish and no longer depend on repeated `sp.URL` / `op.URL` reads in the patched handlers.
- New guard: `BankPanelCompletionURLSnapshotV785Tests`.
- External release boundaries remain unchanged: strict `auval`, Logic runtime, VST3 SDK/toolchain validation, and notarization are still pending until logs prove them.

# ArpSID 0.0.690 pass360 — CoreMIDI main-queue weak continuations

source suite: PASS (313/313 on macOS) carried forward from user-confirmed Mac run.
AUv2 bundle build/install path completed: PASS carried forward.
AU cache refresh: PASS carried forward.
auval: PENDING.
Logic runtime: PENDING.
Do not claim this sandbox ran AUv2/Logic validation.


Post-handoff guards carried forward:
VstCocoaFactoryPresetRangeV759Tests
DrumContextLegacyCanonicalSplitV760Tests
SidcoreSingleAuthoritySurfaceV761Tests
WrapperStateApplyPolicyCoreV762Tests
DigiSplitProtocolSurfaceV763Tests
ParameterDirtyFlushFallbackTelemetryV764Tests
Auv2RampAnchorDropTelemetryV765Tests
Post18ClosureStatusConsistencyV766Tests
SourceOnlyClosureV767Tests
RootBuildScriptReleaseV768Tests
RootBuildScriptReleaseModesV769Tests
MacosInstallStatusHandoffV770Tests
RootBuildScriptAuvalModeV771Tests
RootBuildScriptMacosClosureV772Tests
RootBuildScriptMacosClosureLoggingV773Tests
MacosBuildGreenStatusV774Tests
RootBuildScriptClosureLogCommandV775Tests
ReadmeCurrentVersionNoteV776Tests
DigiAudioQueueWeakContextV777Tests
StandaloneCoreMIDIWeakContextV778Tests
DigiAQBackendCallbackContextV779Tests
PsidAsyncLoadKernelLifetimeV780Tests
BankDocumentWorkerPureHelpersV781Tests
BankWorkerURLSnapshotV782Tests

### Current: pass360 / fix-order #38

- Version: `0.0.690-pass360-rt-safety-audit-coremidi-mainqueue-weak-continuations`
- Latest closed fix: standalone host preset import/export panel completion blocks snapshot URL/extension values and weak-load the app delegate before AU state export/import.
- Previous pass358 bank/DrSID-kit worker URL snapshot hardening is carried forward.


### Current: pass357 / fix-order #35

- Closed: bank document worker pure helpers. Async bank import/export file/JSON work no longer depends on `ArpSIDViewController` instance methods on worker queues.
- Added: `BankDocumentWorkerPureHelpersV781Tests`.
- Remaining external release work: macOS closure log / strict auval / Logic runtime / VST3 SDK / notarization.

# pass356 status — fix-order #34 PSID async load kernel lifetime

- Fix-order #34: AUv3/standalone PSID async load captures a `std::shared_ptr<ArpSIDDSPKernel>` lifetime token instead of a raw `_kernel.get()` pointer before dispatching background work.
- Remaining external release boundaries stay unchanged: `auval`, Logic runtime, VST3 SDK/toolchain validation, and notarization remain pending until logs prove them.

# ArpSID 0.0.690 pass356 — AQCapture backend callback context

- Version: `0.0.690-pass356-rt-safety-audit-aqcapture-backend-context`
- Fix-order #33: the lower-level DIGI `AQCapture` backend no longer registers raw `this` with AudioQueue input/property callbacks. It now uses a stable atomic callback context and invalidates owner/queue before disposal.
- Guard: `DigiAQBackendCallbackContextV779Tests`.
- Remaining external release boundaries stay unchanged: `auval`, Logic runtime, VST3 SDK/toolchain validation, and notarization remain pending until logs prove them.

# ArpSID 0.0.690 pass354 — standalone CoreMIDI weak callback context

- Version: `0.0.690-pass354-rt-safety-audit-host-midi-weak-context`
- Fix-order #32: standalone CoreMIDI hot-plug/read callbacks now use retained weak-delegate context boxes instead of raw `(__bridge void*)self`.
- New guard: `StandaloneCoreMIDIWeakContextV778Tests`.

Current truth state remains:

```text
source suite: PASS (313/313 on macOS, from user-reported pass344/pass348 lineage)
AUv2 install: PASS
AUv2 bundle build/install path completed
AU cache refresh: PASS
macOS build after pass348: PASS (user-confirmed)
auval: PENDING
Logic runtime: PENDING
VST3 SDK/toolchain validation: PENDING
notarization: PENDING
```

## Fix-order / release train closure

- #1–#30: carried forward from pass352.
- #31: DIGI AudioQueue weak callback context. Guard: `DigiAudioQueueWeakContextV777Tests`.

Next Mac command:

```bash
./build.sh --macos-closure --closure-log-dir ./release-logs
```

---

# ArpSID 0.0.690 pass352 — current-version note cleanup

- Version: `0.0.690-pass352-rt-safety-audit-current-version-note`
- Fix-order #30: README source-folder canonical-version note now says current pass352 instead of stale pass350.
- New guard: `ReadmeCurrentVersionNoteV776Tests`.

Current truth state remains:

```text
source suite: PASS (313/313 on macOS, from user-reported pass344/pass348 lineage)
AUv2 install: PASS
AUv2 bundle build/install path completed
AU cache refresh: PASS
macOS build after pass348: PASS (user-confirmed)
auval: PENDING
Logic runtime: PENDING
VST3 SDK/toolchain validation: PENDING
notarization: PENDING
```

## Fix-order / release train closure

- #1–#29: carried forward from pass351.
- #30: current-version note cleanup. Guard: `ReadmeCurrentVersionNoteV776Tests`.

Next Mac command:

```bash
./build.sh --macos-closure --closure-log-dir ./release-logs
```

---

# ArpSID 0.0.690 pass350 — macOS closure log command preservation

- Version: `0.0.690-pass350-rt-safety-audit-closure-log-command`
- Fix-order #28: `build.sh` now snapshots original argv before option parsing and writes the shell-escaped original command to `macos-closure-*.log`.
- New guard: `RootBuildScriptClosureLogCommandV775Tests`.

Why this mattered: pass348/pass349 closure logging captured the log output, but the command banner used `$*` after all options had been shifted away. The log could therefore omit critical flags like `--macos-closure`, `--closure-log-dir`, `--install-auv2`, `--clear-au-cache`, or `--validate-auv2`. pass350 makes the generated handoff log forensic enough to prove which release mode was executed.

Current truth state remains:

```text
source suite: PASS (313/313 on macOS)
AUv2 install: PASS
AU cache refresh: PASS
macOS build after pass348: PASS (user-confirmed)
auval: PENDING
Logic runtime: PENDING
VST3 SDK/toolchain validation: PENDING
notarization: PENDING
```

Next Mac command:

```bash
./build.sh --macos-closure --closure-log-dir ./release-logs
```

## Fix-order / release train closure

- #1–#27: carried forward from pass349.
- #28: macOS closure log command preservation. Guard: `RootBuildScriptClosureLogCommandV775Tests`.

## Remaining external release work

- Run `./build.sh --macos-closure --closure-log-dir ./release-logs` on macOS and keep the generated `macos-closure-*.log`.
- Launch/reload Logic and exercise the installed AUv2 runtime.
- VST3 target still requires the VST3 SDK/toolchain.

---

# ArpSID 0.0.690 pass349 — macOS build-green handoff status

- Version: `0.0.690-pass349-rt-safety-audit-macos-build-green`
- pass349 carries forward the pass344/pass345 macOS full-suite + AUv2 install status, the pass347 full Mac closure command, pass348 closure logging, and records the user-confirmed build-green handoff.
- New guard: `MacosBuildGreenStatusV774Tests`.

Mac lineage status confirmed by user log:

```text
source suite: PASS (313/313 on macOS)
AUv2 install: PASS
AU cache refresh: PASS
macOS build after pass348: PASS (user-confirmed)
auval: PENDING
Logic runtime: PENDING
```

Next full closure command on Mac:

```bash
./build.sh --macos-closure
# or capture to an explicit handoff directory:
./build.sh --macos-closure --closure-log-dir ./release-logs
```

Targeted auval-only command:

```bash
./build.sh --validate-auv2
```

Full rebuild/install/validate command without the release-check/full-CTest wrapper:

```bash
./build.sh --install-auv2 --clear-au-cache --validate-auv2
```

Do not claim auval success, Logic runtime validation, notarization, or VST3 validation until those logs are provided. Do not claim this sandbox ran AUv2/Logic validation.

## Fix-order / release train closure

- #1–#11: carried forward from pass333.
- #12: VST Cocoa preset range uses canonical 180 slots. Guard: `VstCocoaFactoryPresetRangeV759Tests`.
- #13: DrSID legacy/canonical context split. Guard: `DrumContextLegacyCanonicalSplitV760Tests`.
- #14: SID-core mutable surface cleanup. Guard: `SidcoreSingleAuthoritySurfaceV761Tests`.
- #15: wrapper-owned state-apply policy moved to core. Guard: `WrapperStateApplyPolicyCoreV762Tests`.
- #16: DIGI atomic model+sample-bank protocol. Guard: `DigiSplitProtocolSurfaceV763Tests`.
- #17: parameter dirty-flush fallback telemetry. Guard: `ParameterDirtyFlushFallbackTelemetryV764Tests`.
- #18: AUv2 ramp-anchor/drop telemetry. Guard: `Auv2RampAnchorDropTelemetryV765Tests`.
- #19: post-#18 closure/status consistency. Guard: `Post18ClosureStatusConsistencyV766Tests`.
- #20: source-only release closure/handoff truth. Guard: `SourceOnlyClosureV767Tests`.
- #21: root `build.sh` included and executable. Guard: `RootBuildScriptReleaseV768Tests`.
- #22: build helper release/package modes restored. Guard: `RootBuildScriptReleaseModesV769Tests`.
- #23: macOS install handoff status recorded. Guard: `MacosInstallStatusHandoffV770Tests`.
- #24: auval command handoff added. Guard: `RootBuildScriptAuvalModeV771Tests`.
- #25: full macOS closure command added. Guard: `RootBuildScriptMacosClosureV772Tests`.
- #26: macOS closure log capture added. Guard: `RootBuildScriptMacosClosureLoggingV773Tests`.
- #27: macOS build-green handoff recorded. Guard: `MacosBuildGreenStatusV774Tests`.

## Remaining external release work

- Run `./build.sh --macos-closure --closure-log-dir ./release-logs` on macOS and keep the generated `macos-closure-*.log`.
- Launch/reload Logic and exercise the installed AUv2 runtime.
- VST3 target still requires the VST3 SDK/toolchain.


### Fix-order #32 — standalone CoreMIDI weak callback context — CLOSED in pass354

`source/au3/ArpSIDHostAppDelegate.mm` no longer passes raw `(__bridge void*)self` as the CoreMIDI
notification/read refCon.  It uses a retained weak-box context, weak-loads the delegate in both
`ArpSIDMIDINotifyProc` and `ArpSIDMIDIReadProc`, and releases the retained context after MIDI teardown.
Guard: `StandaloneCoreMIDIWeakContextV778Tests`.


### Fix-order #33 — AQCapture backend callback context — CLOSED in pass356

`source/au3/ArpSIDDigiAudioQueueCapture.mm` no longer registers raw `this` as the AudioQueue input/property-listener `userData`. The backend owns a stable `AQCallbackContext` with atomic owner, active-queue, stopping, and callback-in-flight fields. Static callbacks now resolve through that context and return safely when teardown has invalidated the owner. Guard: `DigiAQBackendCallbackContextV779Tests`.

### pass360 / fix-order #38 — CoreMIDI main-queue weak continuations

- Fixed standalone CoreMIDI main-queue continuation lifetime: MIDI activity and CC65/CC67 continuations now weak-capture the delegate and strong-load on main before touching AU/UI state.
- This closes the remaining direct-`self` async continuation in the standalone MIDI path after the v778 CoreMIDI refCon weak-box fix.
- Guard: `StandaloneCoreMIDIMainQueueWeakContinuationsV784Tests`.

### pass362 / fix-order #40 — C64 SID panel URL snapshot

- Closed the remaining C64 SID player panel completion URL surface.
- `_c64LoadSidFile:` now snapshots `sidURL` and `sidFileName` before file IO/status update.
- Guard: `C64SidPanelURLSnapshotV786Tests`.
- External release status remains: auval PENDING, Logic runtime PENDING, VST3 SDK/toolchain validation PENDING, notarization PENDING.

## pass363 / fix-order #41 — DIGI record auto-stop weak continuations

- Fixed DIGI record auto-stop continuations queued from AudioQueue and AVAudioEngine callbacks so they no longer retain callback-local controller references into the main queue.
- Queued stops now use `stopWeakSelf`, strong-load on main, and compare `_digiRecordGeneration_v184_` against the callback/session generation before stopping.
- Guard: `DigiRecordAutostopWeakContinuationV787Tests`.

## pass364 / fix-order #42 — CVDisplayLink main-queue weak continuation

- Fixed the remaining CVDisplayLink main-queue continuation lifetime: the CoreVideo callback no longer retains the controller before dispatching `_poll` to the main queue.
- The queued poll now weak-captures the controller, strong-loads on main, returns if the editor is gone, and releases `_tryBeginPollTick_v246_` through `_endPollTick_v246_` in `@finally` when still alive.
- Guard: `CVDisplayLinkMainQueueWeakContinuationV788Tests`; v731 compile guard updated accordingly.
- External release status remains: auval PENDING, Logic runtime PENDING, VST3 SDK/toolchain validation PENDING, notarization PENDING.

## pass365 / fix-order #43 — bank status main-queue weak continuations

Status: complete.

Next runtime lifetime cleanup after pass364. The remaining synchronous bank/preset save/export handlers no longer queue delayed main-thread status blocks that retain the handler-local strong controller `s`; they weak-load from `ws` on main before touching `_pBank`. Guard: `BankStatusMainQueueWeakContinuationV789Tests`.



## pass366 / fix-order #44 — standalone CoreMIDI notify/menu weak continuations

Status: complete. The standalone CoreMIDI hot-plug notify continuation and preset next/previous menu continuations now use weak-loaded main-queue blocks instead of retaining the app delegate via callback-local strong objects or implicit `_audioUnit` ivar access. Guard: `StandaloneMenuMIDINotifyWeakContinuationsV790Tests`.


## pass367 / fix-order #45 — AU3 requestViewController weak dispatch

Status: complete. `source/au3/ArpSIDAudioUnit.mm` no longer lets the queued `requestViewControllerWithCompletionHandler:` main-thread builder retain/use the AU object through direct `self` capture. It now captures `__weak ArpSIDAudioUnit* weakAudioUnit_v791`, strong-loads it on main, completes with `nil` if the AU was torn down, and passes the weak-loaded AU into both the AUv3 `setExtensionAudioUnit:` path and AUv2 `connectAudioUnit:` path. Guard: `AU3RequestViewControllerWeakDispatchV791Tests`.

## pass369 / fix-order #47 — embedded VST focus weak continuation

Done. The embedded VST open path delayed focus continuation is now weak-only and no longer retains the host parent view, child view, handle, or controller after editor close. Guard: `EmbeddedVSTFocusWeakContinuationV793Tests`.


## pass370 / fix-order #48 — standalone startup/window weak continuations

Done. The standalone host startup repaint/focus and delayed window-show polish blocks now use weak-loaded main-queue continuations instead of capturing the app delegate through `self->_viewController` and `self->_mainWindow`. Guard: `StandaloneStartupWindowWeakContinuationsV794Tests`.

External release status remains: auval PENDING, Logic runtime PENDING, VST3 SDK/toolchain validation PENDING, notarization PENDING.

## pass371 / fix-order #49 — AUv2 view-factory weak AU continuation

Closed the AUv2 Cocoa view factory queued-main-thread AU lifetime surface. Off-main editor construction now captures a zeroing weak AU token and weak-loads it inside both the build and abandoned-editor disposal paths. Next step remains macOS closure/auval/runtime validation.


## pass375 / fix-order #53 — absolute P2 closure audit

- Added `AbsoluteP2ClosureV799Tests`.
- Verified all known source-code-fixable P2 items are fixed, scoped-closed, or accepted-by-design.
- Preserved honest external-boundary status for AUv2/AUv3/Logic/auval, VST3 SDK/toolchain validation, and notarization.
- Made P0/P1 absolute closure guards version-robust before the pass375 bump.


## pass378 / fix-order #56 — C64SidPlayer five-flavor P0 closure

Fixed P0 five-flavor regressions found in the fresh pass377 audit: GUI flavor sync now decodes raw flavor 4 via `componentFlavorFromRaw`, AUv2 factory preset cache uses `kComponentFlavorCount` and `componentFlavorIndex`, and AUv2/AUv3/GUI factory-slot policy explicitly handles `ComponentFlavor::C64SidPlayer` as a dedicated default-slot-only product flavor instead of falling through to SID-808/Classic behavior. Guard: `C64SidPlayerFiveFlavorPolicyV802Tests`.

## pass380 / fix-order #58 — render apply RT-only helper closure

- Fixed fresh-audit P0 #5: render-drained state apply no longer calls documented Non-RT/may-allocate helpers.
- Added RT-only hydrated parameter read/write helpers and guarded `applyStateRootBySwap()` against semantic/vector canonicalization calls.
- Guard: `RenderApplyRTOnlyHelpersV804Tests`.

## pass380 V819 source closure update

- `FactoryBankAudioAuditV687Tests` is closure-bounded by default and keeps exhaustive all-slot audio soak behind `ARPSID_FACTORY_BANK_FULL_AUDIO_AUDIT=1`.
- Removed root-level C64 scratch/demo artifacts that were not part of the ArpSID source product.
- Regenerated release manifest after cleanup.
- Added `ReleaseDeadFileClosureV819Tests`.
- Source-side closure is complete through V819; macOS AU/Logic/notarization closure remains platform-pending.
