# ArpSID v0.0.605 pass83 full-preflight and stale 128 wording closure note

Current package truth:
- Source package: pass83 on v0.0.605.
- Added pass83 full CTest preflight script with serial retry logging.
- macOS production script now uses pass83 preflight.
- Remaining unqualified `full 128-slot factory bank` wording removed from source/CMake/audit comments.
- Added FullPreflightPass83AndStale128V697Tests.
- Focused preflight/stale-wording/deep-payload regression validation: 7/7 passed.
- CTest inventory in this environment: 229 tests.

Historical content below may mention older pass/test counts and is retained only for chronology.

# ArpSID v0.0.605 pass82 voice-policy bounds preflight closure note

Current package truth:
- Source package: pass82 on v0.0.605.
- GCC14 static analyzer warning in VoiceAllocator::allNotesOffChannel() fixed by bounded scratch compaction.
- Added VoicePolicyAllNotesOffChannelBoundsV696Tests.
- Focused targeted regression validation: 8/8 passed.
- CTest inventory in this environment: 228 tests.
- Full all-target build in sandbox still timed out on heavy DSP-kernel tests; targeted failing/warning surface builds and passes.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass81 deep factory payload closure note

Current package truth:
- Source package: pass81 on v0.0.605.
- Digi 150..179 now has real DigiPanelModel payloads, not only identity/default params.
- DrSID 80..119 now has behaviorally distinct register-microprogram payloads.
- Digi signature fields are projected into source/start/length/tune/flags in the Digi model.
- DrumEngineHostBridge Digi rejection is explicit policy and tested.
- Dead 127-based kParamBankSlot decode removed from processor automation code.
- Focused deep factory payload regression validation: 15/15 passed.
- CTest inventory in this environment: 227 tests.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass80 context-aware legacy test closure note

Current package truth:
- Source package: pass80 on v0.0.605.
- Mac full-CTest failures V520/V527 fixed by updating stale test assumptions to pass78 context-aware payload semantics.
- Canonical DrSID 80..119 is authored.
- Legacy 120..124 remains compatibility-only while canonical context is SID808.
- Digi Drum-role slots 150..179 are intentionally non-DrSID.
- Added LegacyDrumTestsContextAwareV690Tests.
- Focused context/payload regression validation: 12/12 passed.
- CTest inventory in this environment: 221 tests.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass79 factory payload final preflight closure note

Current package truth:
- Source package: pass79 on v0.0.605.
- Pass78 payload completeness was verified and tightened.
- VST3 factory preset list explicitly uses canonical 180-slot count.
- .arpbank v1 export is explicitly documented as 128-slot user-bank-compatible subset, not full 180-slot factory.
- Added FactoryPayloadFinalClosureGuardV689Tests.
- Focused payload/factory/kit/export regression validation: 16/16 passed.
- CTest inventory in this environment: 220 tests.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass78 factory payload completeness closure note

Current package truth:
- Source package: pass78 on v0.0.605.
- DrSID canonical range 80..119 has authored DrSID factory payloads.
- Digi canonical range 150..179 is no longer sanitized into DrSID mode and has explicit Digi factory defaults.
- VST3 program list exposes canonical factory count.
- .arpbank v1 export is documented as a 128-slot user-bank-compatible subset.
- KIT sequencer rejects malformed panel/voice models at compile boundary.
- Added FactoryDrSidCanonicalPayloadV683Tests through KitSequencerModelValidationV688Tests.
- Focused factory-payload regression validation: 13/13 passed.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass77 final UX polish closure note

Current package truth:
- Source package: pass77 on v0.0.605.
- Final P2 UX issues from pass75 audit fixed.
- Bank group status now labels 1-based ranges as display slots.
- Canonical SID808 slots 120..149 all receive priority styling.
- Added GuiBankSegmentUxPolishV682Tests.
- Focused final UX/factory/warning regression validation: 12/12 passed.
- CTest inventory in this environment: 213 tests.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass76 warning-clean test closure note

Current package truth:
- Source package: pass76 on v0.0.605.
- AppleClang unused/set-but-unused warnings in V548/V550 tests fixed at root.
- Assert-only variable usage replaced with explicit checks.
- Added WarningCleanTestContractV681Tests.
- Focused warning-clean/factory/GUI regression validation: 10/10 passed.
- CTest inventory in this environment: 212 tests.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass75 canonical bank segment layout closure note

Current package truth:
- Source package: pass75 on v0.0.605.
- Factory bank segmented selector now exposes the full non-overlapping canonical layout.
- PADS segment removed because it overlapped DRSID 80..119.
- SID808 and DIGI are reachable from the segmented selector.
- Added GuiBankSegmentCanonicalLayoutV680Tests.
- Focused GUI bank/factory/Program regression validation: 12/12 passed.
- CTest inventory in this environment: 211 tests.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass74 legacy Program expectation closure note

Current package truth:
- Source package: pass74 on v0.0.605.
- V238/V240 stale Program expectations updated from 7-bit MIDI helper to canonical factory helper.
- kParamProgram as factory identity mirror is 0..179.
- Added LegacyProgramExpectationClosedV679Tests.
- Focused Program/BankSlot/factory identity regression validation: 12/12 passed.
- CTest inventory in this environment: 210 tests.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass73 Mac GUI color compile closure note

Current package truth:
- Source package: pass73 on v0.0.605.
- Fixed macOS AUv2 compile failure caused by undefined c64LtRed().
- DRSID bank group now uses existing c64Red().
- Added GuiColorAndBoundaryCompileGuardV678Tests.
- Focused factory/bank/GUI regression validation: 12/12 passed.
- CTest inventory in this environment: 209 tests.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass72 file-bank/factory boundary closure note

Current package truth:
- Source package: pass72 on v0.0.605.
- Factory preset space is 0..179.
- v1 .arpsidbank user-bank space is 0..127.
- Extended factory slots 128..179 are no longer silently aliased into v1 slot 127.
- Added V675/V676/V677 tests for bank persistence boundary, label refresh, and text/tooltips.
- Focused bank/factory boundary regression validation: 14/14 passed.
- CTest inventory in this environment: 208 tests.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass71 final factory identity sweep closure note

Current package truth:
- Source package: pass71 on v0.0.605.
- Final sweep found and fixed two remaining GUI factory identity leftovers.
- GUI bank groups now explicitly expose DRSID, SID808, and DIGI canonical ranges.
- Added FactoryIdentityFinalSweepV674Tests.
- Focused final factory identity regression validation: 12/12 passed.
- CTest inventory in this environment: 205 tests.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass70 GUI/AUv2 factory slot identity closure note

Current package truth:
- Source package: pass70 on v0.0.605.
- Remaining audit-reported GUI/AUv2/preset/program 0..127 assumptions were fixed.
- Added V671/V672/V673 tests for concrete apply/defer/readback/AUv2/program metadata paths.
- Focused GUI/AUv2/factory identity regression validation: 12/12 passed.
- CTest inventory in this environment: 204 tests.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass69 full-build/full-CTest green closure note

Current package truth:
- Source package: pass69 on v0.0.605.
- Full default CMake build completed successfully.
- Full CTest completed successfully: 201/201 passed.
- MacOSProductionScriptV653Tests stale ctest-string contract fixed after pass68 preflight delegation.
- This is Linux/GCC/CMake proof; macOS AUv2/Logic/auval still requires running ./build.sh on Mac.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass68 full-CTest preflight closure note

Current package truth:
- Source package: pass68 on v0.0.605.
- Added hard full-CTest preflight script.
- macOS production build path now calls pass68 preflight before AU install/auval.
- Added FullCTestPreflightScriptV670Tests.
- Focused preflight/factory/AU/UI regression validation: 13/13 passed.
- CTest inventory in this environment: 201 tests.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass67 extended factory slot identity closure note

Current package truth:
- Source package: pass67 on v0.0.605.
- Factory slot identity now roundtrips 0..179, not 0..127.
- Slots 127, 128, 149, 150, and 179 remain distinct through normalized state-root identity.
- AU/GUI/VST/FileBank metadata paths use canonical 180-slot helpers.
- Focused extended factory identity regression validation: 12/12 passed.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass66 full-CTest factory regression closure note

Current package truth:
- Source package: pass66 on v0.0.605.
- Fixes full-CTest failure in ForensicEngineSanityV527Tests.
- Fixes full-CTest failure in AuthenticBassSid808LogicV241Tests.
- Old 128-slot and exact-wrap SID808 test assumptions now follow canonical factory/SID808 variant truth.
- Focused full-CTest regression validation: 12/12 passed.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass65 SID808 param parity + DrSID pre-authored KIT trigger note

Current package truth:
- Source package: pass65 on v0.0.605.
- SID808 normalized factory params derive from factory Sid808VoiceConfig signatures.
- DrSID KIT trigger now builds final authored voice program before applying SID registers.
- Focused factory/KIT/DrSID regression validation: 12/12 passed.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass64 factory KIT authority closure note

Current package truth:
- Source package: pass64 on v0.0.605.
- Factory/state-root range now covers canonical drum slots through 179.
- SID808 PatchDefinitions cover 120..149.
- Digi PatchDefinitions cover 150..179.
- State-root and DSP reset preserve canonical drum slots.
- GUI factory grid uses kFactoryPatchSlotCount, not hardcoded 128.
- Legacy slot 127 schema discriminator is explicit.
- DrSID register image mirrors ring/sync bits.
- Focused factory/KIT regression validation: 12/12 passed.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass63 full KIT runtime resolver note

Current package truth:
- Source package: pass63 on v0.0.605.
- Added include/arpsid/gui/kit_runtime_resolver.h.
- Added KitRuntimeResolverV657Tests.
- KIT now has a single runtime resolution contract for compiled hits.
- Focused KIT/full-regression validation: 12/12 passed.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass62 full-CTest reported failures closure note

Current package truth:
- Source package: pass62 on v0.0.605.
- Fixes full-CTest failure in Sid808EngineAndRouterV535Tests.
- Fixes stale source-contract in GuiViewControllerWiringV590Tests.
- Adds DrSidAllNotesOffAllocatorResetV656Tests.
- Reported failing tests now pass.
- Focused reported-failure closure validation: 16/16 passed.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass61 macOS build wrapper path closure note

Current package truth:
- Source package: pass61 on v0.0.605.
- Fixes root build.sh path mismatch.
- build.sh now searches multiple helper locations and runs them via bash.
- Adds scripts/macos compatibility aliases.
- Added MacOSBuildWrapperPathsV655Tests.
- Focused wrapper validation: 3/3 passed.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass60 bridge DrSID legacy call-site closure note

Current package truth:
- Source package: pass60 on v0.0.605.
- Fixes full-build break in DrSidRestoreAndHostBridgeV537Tests after drsidEngine() alias removal.
- Historical V537 test now uses bridgeDiagnosticDrsidEngine().
- Added BridgeDrsidLegacyCallsiteGuardV654Tests.
- Focused closure validation: 23/23 passed.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass59 production validation harness note

Current package truth:
- Source package: pass59 on v0.0.605.
- Added robust root build.sh with correct shebang.
- Added macOS full build/install/cache-clear/auval production script.
- Hardened full closure validation script.
- Focused closure validation: 21/21 passed.
- Actual AUv2/Logic/auval proof still must be produced on macOS by running build.sh.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass58 explicit stem-mix policy note

Current package truth:
- Source package: pass58 on v0.0.605.
- Added RT-safe drum stem mixer contract.
- Added explicit DSP-kernel flavor-to-stem-policy resolver.
- Focused closure validation: 20/20 passed.
- This is an explicit policy contract, not a full AU graph rewrite into physical stems everywhere.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass57 code-fixable outstanding closure note

Current package truth:
- Source package: pass57 on v0.0.605.
- DrSID KIT voice payload now reaches actual SID synthesis registers.
- DrSID selected factory slot has an internal kit-voice config contract.
- KIT Digi one-shot projection is dense slot0, not sparse nonzero projection.
- bridge-local drsidEngine() alias is removed by default.
- Added mixed-target runtime validation for SID808/DrSID/Digi routing.
- Focused closure validation: 18/18 passed.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass56 audit-closure matrix note

Current package truth:
- Source package: pass56 on v0.0.605.
- Added docs/audit/AUDIT_FINDINGS_CLOSURE_MATRIX.md/json.
- Added scripts/check_audit_closure.py.
- Added AuditClosureMatrixV646Tests.
- Focused audit-closure validation: 14/14 passed.
- Code-fixable P1 audit findings are closed or scoped-closed with evidence.
- macOS AUv2/AUv3/Logic/auval and full RSID exactness are explicitly not claimed fixed in this container.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass55 full closure validation harness note

Current package truth:
- Source package: pass55 on v0.0.605.
- Added scripts/run_full_closure_validation.sh.
- Added FullClosureManifestV645Tests.
- Focused closure validation: 13/13 passed in this container.
- Full default all-target build may exceed this chat execution window; the script is included for local full-run validation.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass54 runtime authority closure note

Current package truth:
- Source package: pass54 on v0.0.605.
- Focused runtime/contract closure build/test: 12/12 passed.
- Added DrumBridgeRuntimeAuthorityV644Tests.
- Bridge replacing render semantics are now proven at runtime, not only by source-shape checks.
- Pass53 target-authoritative KIT routing remains green.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass53 fullworthy correctness closure note

Current package truth:
- Source package: pass53 on v0.0.605.
- Focused fullworthy closure build/test: 11/11 passed.
- KIT routing is now target-authoritative: SID808→SID808, DrSID→DrSID, Digi→Digi.
- DrumMachine component flavor no longer forces non-SID808 KIT events into DrSID.
- Existing pass52 closure tests remain green.
- PASS53_FULLWORTHY_CORRECTNESS_MANIFEST.md records the final semantic closure.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass52 final closure note

Current package truth:
- Source package: pass52 on v0.0.605.
- Focused final closure build/test: 9/9 passed.
- Digi nonzero-slot runtime ambiguity is fixed in both kernel one-shot projection and DigiSampler triggerSlotAt path.
- SID808 projection sync is guarded by contract test to ensure it cannot fight per-hit selected slot overrides.
- DrumBridgeNoSilenceV613Tests is now included in the final focused closure validation.
- PASS52_FINAL_CLOSURE_MANIFEST.md records closed items and the remaining macOS/stem/RSID limits honestly.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass51 DrSID factory-slot audio-shape closure note

Current package truth:
- Source package: pass51 on v0.0.605.
- DrSID selected factory slot now affects actual DrSID runtime shape, not only diagnostics.
- Existing DrSID KIT payload diagnostics are retained.
- Existing SID808 factory-vs-KIT override precedence and Digi nonzero-slot fixes remain green.
- DSP multi-TU kernel smoke remains green.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass50 P1 KIT/DrSID/SID808/Digi closure note

Current package truth:
- Source package: pass50 on v0.0.605.
- CTest discovery in this pass includes the new v637-v639 closure tests.
- SID808 selected factory slot now remains the base voice; default KIT voice values no longer mask it.
- KIT voice payload now uses runtime override masks.
- DrSID KIT branch now records selected factory slot and voice payload through a dedicated trigger path while retaining audible GM trigger behavior.
- Digi one-shot projection no longer reports activeSlotCount=1 when using a nonzero slot index.
- Bridge DrSID compatibility accessor is marked deprecated.
- Existing DSP multi-TU and KIT hash contract tests remain green.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass49 CMake/source-root guard closure note

Current package truth:
- Source package: pass49 on v0.0.605.
- CTest discovery in this pass: 167 tests.
- CMake configure now fails immediately if stale AUv2-breaking DSP-kernel field refs are present.
- Added explicit `arpsid_verify_source_tree` build target.
- Hardened macOS AUv2 script to print source root and run both Python and CMake guard before build/install.
- Added clean-room unzip/build helper to avoid accidentally building old numbered folders.
- Existing source-tree, multi-TU DSP kernel, and KIT hash contract tests remain green.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass48 stale-source guard closure note

Current package truth:
- Source package: pass48 on v0.0.605.
- CTest discovery in this pass: 166 tests.
- Added hard source-tree guard so old folders with stale AUv2-breaking `ArpSIDDSPKernel.hpp` fail before configure/build/install.
- Hardened macOS AUv2 script to run the guard before configure and refuse install if build fails.
- Added PASS48 manifest with kernel hash and forbidden/required refs.
- Existing DSP kernel single-TU, multi-TU, and KIT hash contract tests remain green.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass47 multi-TU DSP-kernel smoke closure note

Current package truth:
- Source package: pass47 on v0.0.605.
- CTest discovery in this pass: 165 tests.
- Added a multi-translation-unit DSP kernel include/link smoke that mimics AUv2/AUv3 including `ArpSIDDSPKernel.hpp` from several translation units.
- Retained single-TU kernel include smoke and KIT POD hash field contract test.
- Added macOS AUv2 build/install/cache cleanup script to prevent duplicate component class collisions.
- Existing KIT assign/voice/SID808 override tests remain green.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass46 DSP-kernel compile-smoke closure note

Current package truth:
- Source package: pass46 on v0.0.605.
- CTest discovery in this pass: 164 tests.
- Added a portable `ArpSIDDSPKernel.hpp` include/compile smoke test.
- This catches AUv2/AUv3 header field-contract drift without requiring Apple AU SDK in CI.
- Existing KIT hash/POD/SID808 override tests remain green.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass45 POD/field contract closure note

Current package truth:
- Source package: pass45 on v0.0.605.
- CTest discovery in this pass: 163 tests.
- Continued from pass44 and tightened POD/field-contract validation around KIT assignment/voice/assign-grid structs.
- AUv2 KIT hash field drift is fixed at the underlying struct-contract level.
- Added dedicated `KitHashFieldContractV632Tests` to catch real POD field drift before AUv2/ObjC++ builds.
- Existing KIT assign, voice, migration, mixed-target, selected-slot and SID808 override tests remain green.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass44 AUv2 KIT hash field-contract fix

Current package truth:
- Source package: pass44 on v0.0.605.
- CTest discovery in this pass: 163 tests.
- Fixed AUv2/ObjC++ build break in `ArpSIDDSPKernel.hpp` KIT hash.
- Removed references to nonexistent `KitDrumClassAssignment::{userSlotIndex,flags,pad}`.
- Removed references to nonexistent `KitVoiceConfig::pulseWidth`.
- Added `KitHashFieldContractV632Tests` to pin real POD field names and hash sensitivity.
- Existing KIT assign/voice/migration/mixed-target/selected-slot/SID808 override tests still pass.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass43 full closure sweep note

Current package truth:
- Source package: pass43 on v0.0.605.
- CTest discovery in this pass: 162 tests.
- Production-code scan for TODO/FIXME/placeholder/scaffold/stub/not-implemented/unimplemented remains clean.
- All stale tab architecture tests were updated for the no-scaffold production contract.
- Broad GUI/tab + KIT/drum/DrSID/SID808/Digi closure target set builds and passes.
- A monolithic all-target build still exceeds this execution window, so validation is chunked by closure area.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass42 no-scaffold/no-placeholder closure note

Current package truth:
- Source package: pass42 on v0.0.605.
- CTest discovery in this pass: 162 tests.
- Production code scan for TODO/FIXME/placeholder/scaffold/stub/not-implemented/unimplemented is clean.
- External reSID-fp compatibility layer is explicitly named deterministic fallback and exposes diagnostics.
- GUI tab architecture no longer carries a scaffold enum/status; all tabs are production implemented.
- Existing pass37-pass41 KIT/SID808/DrSID/Digi closure tests remain green.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass41 release-closure note

Current package truth:
- Source package: pass41 on v0.0.605.
- CTest discovery in this pass: 160 tests.
- KIT assign schema v2 migration is integrated with the original v559 assign-contract test.
- Stale schema-v1 expectations are updated.
- Broad KIT/drum focused validation covers assign config, migration, mixed targets, selected-slot consumption, voice config, bridge behavior, Digi trigger, DrSID replacing render, and SID808 per-hit override.
- A full all-target build was attempted but exceeds the execution window here; focused closure targets build and pass.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass40 KIT assign migration closure note

Current package truth:
- Source package: pass40 on v0.0.605.
- CTest discovery in this pass: 160 tests.
- KIT assign schema is bumped to v2 because `engineTargetOverride` occupies a former padding byte.
- Old schema-v1 assign blobs are migrated so the old padding byte becomes `255 = follow global target`, not accidental DrSID override.
- `compileKitSequencer()` sanitizes/migrates assign grids before reading target overrides.
- Existing pass39 mixed-target behavior remains intact.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass39 mixed KIT target closure note

Current package truth:
- Source package: pass39 on v0.0.605.
- CTest discovery in this pass: 159 tests.
- KIT assignment config now supports per-drum engine target override.
- Compiled KIT events can mix DrSID, SID808 and Digi targets in the same step.
- Render-side compile consumes `KitAssignConfigGrid`, so mixed target routing reaches audio paths.
- KIT hash includes assignment config target override, so edits rebuild the compiled sequencer.
- Existing pass37/pass38 SID808 override and selected-slot consumption fixes are retained.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass38 KIT selected-slot consumption closure note

Current package truth:
- Source package: pass38 on v0.0.605.
- CTest discovery in this pass: 158 tests.
- KIT selected SID808 factory slot is now consumed per hit through Sid808HitOverride.
- Selected SID808 slot supplies frequency, voice level, waveform, ADSR, pulse width and flags unless explicit per-hit voice payload overrides them.
- KIT Digi path now uses compiled selectedFactorySlot for factorySlotIndex/absoluteFactorySlot instead of confusing it with runtime slot index.
- DrumEngineRouter allNotesOff now calls DrSidEngine::allNotesOff(), not allocator-reset-only.
- Bridge-owned DrSID accessor is explicitly diagnostic to prevent future authority split-brain.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass37 SID808 per-hit override closure note

Current package truth:
- Source package: pass37 on v0.0.605.
- CTest discovery in this pass: 157 tests.
- `Sid808Engine` now consumes per-hit voice overrides.
- Router and host bridge carry per-hit SID808 overrides, including scheduled notes.
- KIT SID808 trigger path converts compiled voice payload into `Sid808HitOverride`.
- KIT voice editor state now reaches the audio-producing router-owned SID808 engine.
- Existing pass31–pass36 DrSID/KIT/Digi/SID808 routing fixes are retained.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass36 KIT voice payload closure note

Current package truth:
- Source package: pass36 on v0.0.605.
- CTest discovery in this pass: 156 tests.
- Compiled KIT events now carry voice-config payload bytes: waveform, AD, SR, pulse width, flags.
- KIT compile has an overload that consumes `KitVoiceConfigGrid`; old callers use default voice grid.
- Render-side KIT compile now passes the live `KitStateBlob::voiceConfigGrid`.
- SID808 KIT trigger path has a stable RT-safe event-boundary wiring point for per-hit voice overrides.
- Existing pass31–pass35 drum/KIT/Digi/DrSID fixes are retained.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass35 DrSID replacing-render closure note

Current package truth:
- Source package: pass35 on v0.0.605.
- CTest discovery in this pass: 156 tests.
- DrSID now has an explicit replacing render wrapper.
- DrumEngineRouter uses DrSID replacing render for DrSID authority mode.
- Inactive DrSID authority render clears stale buffers instead of leaving caller data.
- Additive DrSID `processBlock()` behavior remains available for canonical/layered render paths.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass34 DrSID policy/choke closure note

Current package truth:
- Source package: pass34 on v0.0.605.
- CTest discovery in this pass: 155 tests.
- DrSID unsupported MIDI fallback is explicit and disabled by default.
- Strict unsupported MIDI notes no longer silently map to kick/snare/etc.
- Legacy fallback can be explicitly enabled.
- Hat choke semantics no longer clear Rim GM/semantic state; Rim no longer clears hat semantic ledgers.
- Existing pass31/pass32 drum bridge, KIT, Digi and SID808 authority fixes are retained.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass33 SID808 bridge mix-policy closure note

Current package truth:
- Source package: pass33 on v0.0.605.
- CTest discovery in this pass: 154 tests.
- SID808 bridge output is replacement-authority only for the SID808 flavor.
- DrumMachine flavor adds SID808 bridge audio instead of overwriting the whole bus, preserving canonical/non-drum layers.
- SID808 bridge authority still does not use peak-gating, so silent SID808 blocks cannot leak ghost DrSID tails in SID808 flavor.
- KIT→Digi and pass31 bridge/router authority fixes are retained.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass32 KIT/Digi/SID808 authority closure note

Current package truth:
- Source package: pass32 on v0.0.605.
- CTest discovery in this pass: 153 tests.
- KIT->Digi per-step routing now triggers DigiSamplerEngine directly instead of being dropped or misrouted to drum bridge.
- DigiSamplerEngine exposes sample-accurate `triggerSlotAt()`.
- SID808 bridge replacement no longer uses audio peak as authority; configured SID808 may replace with silence to prevent ghost DrSID tails.
- Existing pass31 DrSID/SID808 bridge/KIT routing fixes are retained.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass31 DrSID/KIT/drum-routing closure note

Current package truth:
- Source package: pass31 on v0.0.605.
- CTest discovery in this pass: 152 tests.
- DrumEngineHostBridge SID model and SID808 factory slot loading now mutate the router-owned render engines.
- DrumEngineRouter is mono/null-right safe and clears authority buffers before render.
- allNotesOff clears scheduled bridge notes.
- Scheduled-note overflow drops/counts instead of firing future notes early.
- KIT compiled events carry engine target and selected factory slot.
- KIT hash includes assignments and voice config state.
- KIT DrSID target routes to canonical `engineBank_.drSid`; KIT SID808 target routes to bridge SID808.
- Dedicated behavior regression pins bridge mono safety and KIT event metadata.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass30 CIA/PHI2 integration closure note

Current package truth:
- Source package: pass30 on v0.0.605.
- CTest discovery in this pass: 151 tests.
- Extended CIA state is now integrated into `C64Phi2Diagnostics` for CIA1 and CIA2.
- CIA1 IRQ and CIA2 NMI-side state, FLAG, TOD, alarm-write-mode, serial and CNT surfaces are visible through PHI2 diagnostics.
- Dedicated `C64CiaPhi2IntegrationV620Tests` pins machine-level integration.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass29 CIA closure note

Current package truth:
- Source package: pass29 on v0.0.605.
- CTest discovery in this pass: 150 tests.
- CIA TOD read-latch behavior, alarm write mode, TOD write normalization, FLAG, serial, CNT, and Timer A/B paths are regression-pinned.
- CIA remains conservatively marked non-cycle-exact for obscure 6526 edge cases.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass28 CIA extended closure note

Current package truth:
- Source package: pass28 on v0.0.605.
- CTest discovery in this pass: 150 tests.
- CIA FLAG, TOD alarm/latch/stop-resume, serial SDR IRQ, CNT, Timer A/B, and force-load paths have dedicated regression coverage.
- CIA remains conservatively marked non-cycle-exact for obscure 6526 edge cases.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass27 CIA correctness closure note

Current package truth:
- Source package: pass27 on v0.0.605.
- CTest discovery in this pass: 149 tests.
- CIA Timer A/B force-load, one-shot, IRQ, CNT-edge, and Timer-B-from-Timer-A-underflow paths have dedicated regression coverage.
- `CiaTimerPhaseSnapshot` exposes timer counters, latches, CNT edge count, just-underflow flags, reload-pending flags, and IRQ level.
- CIA remains conservatively marked non-cycle-exact for obscure 6526 edge cases; this pass improves behavior and observability without making false claims.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass26 PHI2 read-phase boundary closure note

Current package truth:
- Source package: pass26 on v0.0.605.
- CTest discovery in this pass: 148 tests.
- `MemoryMatrix::cpuRead()` is now a complete read bus-event boundary.
- Reused `Phi2BusPhase` objects cannot leak stale SID/RMW/open-bus/vector metadata into later reads.
- `MemoryMatrix::cpuWrite()` remains a complete write bus-event boundary from pass25.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass25 PHI2 write-phase boundary closure note

Current package truth:
- Source package: pass25 on v0.0.605.
- CTest discovery in this pass: 148 tests.
- `MemoryMatrix::cpuWrite()` is now a complete write bus-event boundary.
- Reused `Phi2BusPhase` objects cannot leak stale SID/RMW/open-bus write metadata into later writes.
- Explicit RMW final-write requests are preserved while stale final-write state is cleared.
- Dirty-write logs preserve dummy-old/final-new/normal event kind correctly.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass24 full RMW semantics closure note

Current package truth:
- Source package: pass24 on v0.0.605.
- CTest discovery in this pass: 148 tests.
- CPU-generated RMW operations now expose both dummy-old and final-new bus events.
- Direct MemoryMatrix RMW writes mark their bus phase and dirty-log event correctly.
- Dirty-write records carry full `RmwBusEventKind`, not just a boolean.
- C64SidBridgeState and runtime SID sink both use non-register-RAM OSC3/ENV3 state.
- Dirty-write ring returns chronological order after wrap.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass23 remaining-correctness closure note

Current package truth:
- Source package: pass23 on v0.0.605.
- CTest discovery in this pass: 148 tests.
- C64SidBridgeState now also uses deterministic non-register-RAM OSC3/ENV3 state.
- Dirty-write ring returns chronological order after wrap.
- Normal writes are not mislabeled as RMW final-write events.
- Final regression pins SID open-bus, OSC3/ENV3, dirty ring wrap, RMW semantics.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass22 final correctness closure note

Current package truth:
- Source package: pass22 on v0.0.605.
- CTest discovery in this pass: 148 tests.
- IRQ line rising edges and vector-entry fetches are separate diagnostics.
- PHI2 memory matrix has a dirty-write ring for explicit PHI2-authority / writeback inspection.
- SID write-only reads return open bus; OSC3/ENV3 use deterministic non-register-RAM state.
- RMW bus-event semantics are carried separately as dummy/final write metadata.
- BASIC startup request is reported and execution is not falsely claimed.
- DrSID/SID808 authority remains no-split-brain.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass21 control-plane wiring closure note

Current package truth:
- Source package: pass21 on v0.0.605.
- CTest discovery in this pass: 147 tests.
- IRQ/NMI latch counters and vector-fetch reporting are exposed through PHI2 diagnostics.
- CIA Timer A/B underflow/reload/IRQ-edge phase telemetry is exposed through `CiaTimerPhaseSnapshot`.
- VIC BA/AEC/badline/sprite-DMA cycle-table surface and CIA2->VIC bank/fetch-base propagation are wired.
- PHI2 memory matrix tracks dirty writes for explicit PHI2-authority reporting.
- SID reads/open-bus/RMW/BASIC/PSID synthetic CIA IRQ reporting surfaces are explicit.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass20 final RMW-preservation closure note

Current package truth:
- Source package: pass20 on v0.0.605.
- CTest discovery in this pass: 146 tests.
- RMW SID writes preserve the `rmwDummy` flag in PHI2 sink writes.
- `C64SidBridgeTimedWrite` now carries an explicit `rmwDummy` field; legacy non-PHI2 bridge writes default it to false.
- `C64Runtime::reset()` and `loadPsid()` both clear PHI2/exactness state.
- SID reads are not normal register RAM.
- RSID exactness reports missing real ROMs, BASIC startup flag, CIA approximation, SID-read approximation, and RMW SID writes.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass19 exactness/wiring closure note

Current package truth:
- Source package: pass19 on v0.0.605.
- CTest discovery in this pass: 146 tests.
- `C64Runtime::loadPsid()` is a hard per-load reset boundary.
- SID reads are not normal register RAM; write-only SID registers read bus/fallback values.
- SID read approximation, RMW SID writes, missing real ROMs, BASIC startup flag, and CIA model approximation all downgrade RSID exactness.
- CIA timer/IRQ model is explicitly marked non-cycle-exact until completed.
- PHI2 instruction-budget accounting uses `Cpu6510Micro::retiredInstructionCount()`.
- DrSID / DrumMachine production audio authority: canonical `engineBank_.drSid` only.
- SID808 production audio authority: `drumEngineBridge_.sid808Engine()` only.
- AUv2 render-notify callbacks are skipped unconditionally on the realtime render thread.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass18 final closure note

Current package truth:
- Source package: pass18 final closure on v0.0.605.
- CTest discovery in this pass: 146 tests.
- DrSID / DrumMachine production audio authority: canonical `engineBank_.drSid` only.
- SID808 production audio authority: `drumEngineBridge_.sid808Engine()` only.
- AUv2 render-notify callbacks are skipped unconditionally on the realtime render thread.
- `C64Runtime::loadPsid()` is a full per-load reset boundary.
- PHI2 RSID telemetry wording is “PHI2 active / no known downgrade observed”, not marketing claims of perfect hardware truth.
- `Cpu6510Micro::retiredInstructionCount()` is the source for PHI2 instruction-budget accounting.
- macOS AUv2/AUv3/Logic/auval validation must be run on macOS; this Linux package cannot claim Apple runtime validation.

Historical content below may mention older pass/test counts and is retained only for chronology.


# ArpSID v0.0.605 pass16 / v614 audit closure note

Current package truth:
- Source package: pass16 / v614 cleanup on v0.0.605
- CTest discovery after this pass includes 145 tests when configured with tests enabled.
- DrSID / DrumMachine production audio authority: canonical `engineBank_.drSid` only.
- SID808 production audio authority: `drumEngineBridge_.sid808Engine()` only.
- AUv2 render-notify callbacks are skipped unconditionally on the realtime render thread in release code.
- `C64Runtime::loadPsid()` is a full per-load reset boundary.
- Backup source artifacts such as `*.bak` are not part of this release tree.

Historical content below may mention older v591/v605/pass counts and is retained only for chronology.


# ArpSID 0.0.444 - GUI Tab Architecture

This document is the current specification for the 9-tab GUI architecture.
The canonical inventory lives in `include/arpsid/gui/tab_architecture.h`,
where compile-time `static_assert` pins keep the tab order, labels, drum
contexts, and implementation status locked to the release contract.

All 9 tabs are implemented in the v591 source tree. There are no remaining
GUI scaffold tabs.

## Inventory

| # | Tab | HUD | Status | DrumContext | Notes |
|---|---|---|---|---|---|
| 0 | DRSID | DRSID | Implemented | DrSID_C64Wavetable | Register-microprogram drums on one SID |
| 1 | SID-808 | S808 | Implemented | SID808_AnalogProjection | TR-style analog x0x projection |
| 2 | DIGI | DIGI | Implemented | Digi4Bit | $D418 volume-DAC sample surface |
| 3 | SEQ | SEQ | Implemented | None | Global step sequencer |
| 4 | KIT | KIT | Implemented | None | Kit editor for all drum engines |
| 5 | MIX | MIX | Implemented | None | Per-instrument mixer and FX |
| 6 | SIDCORE | SCORE | Implemented | None | Live SID register timeline |
| 7 | C64 STATE | C64 | Implemented | None | CPU/SID/CIA/PSID state inspector |
| 8 | SETTINGS | SET | Implemented | None | Global preferences and diagnostics |

## Implemented Surfaces

### DRSID

DrSID remains the C64-wavetable drum surface. The tab is backed by the
DrSID instrument-program and kit-compiler contracts, with deterministic
register microprogram playback and factory-bank coverage.

### SID-808

SID-808 remains the analog x0x projection surface. It owns the SID808
engine/router path, transport integration, factory definitions, voice smoke,
accent, hat choke, determinism, and peak-headroom coverage.

### DIGI

DIGI is implemented as the $D418 sample-facing tab. Its GUI model and tab
wire are covered by the v563-v565 tests, including model defaults, tab
projection, and state persistence.

### SEQ

SEQ is implemented as the global sequencer surface with note range, swing,
tempo, rate-law, and step-grid coverage across the v567-v572 tests.

### KIT

KIT is implemented as a full drum-kit editing surface. The model, tab wire,
32-step grid, assign config, voice config, and state blob are covered by the
v555-v560 tests.

### MIX

MIX is implemented as the per-instrument mixer and FX surface. The panel
model, tab wire, FX processors, and state persistence are covered by the
v547-v548, v554, and v561 tests.

### SIDCORE

SIDCORE is implemented as the live SID register/timeline surface. It is fed
from the RT-safe SIDCORE model, ingress ring, scope triple-buffer, and C64
PSID SIDCORE timeline coverage.

### C64 STATE

C64 STATE is implemented as the read-only C64 inspector. It includes
platform/clock/CPU registers, SID model/topology readout, PSID address map,
CIA/IEC/tape/ROM state, SID register mirror, memory-window summary, C64 bus
oscilloscope views, SIDCORE timeline, and audit counters.

### SETTINGS

SETTINGS is implemented as the global preferences and diagnostics surface,
with model, persistence, theme, language, and tab-wire coverage.

## Audit-Correctness Invariants

These invariants apply across all tabs and are pinned through the tab
architecture header and tests:

1. Tab inventory size is exactly 9.
2. Enum order matches array order for state persistence.
3. HUD labels are at most 6 characters.
4. Implementation status is compile-time queryable.
5. `implementedTabCount() == 9`.
6. `scaffoldTabCount() == 0`.
7. Primary `DrumContext` per tab matches the engine-split architecture.
8. GUI never sources truth for engine identity; the engine-layer
   `DrumKitIdentity` always wins and the GUI reflects it.

## Validation

The v591 final source package was built from the applied source tree after
the GUI/tab/button/realtime/scope/telemetry cleanup and the DrSID/SID-808
live drum-data audit. The v590 package added the final top-bar mount and
dedicated DRSID telemetry/overlay wiring pass; v591 adds AUv3 render-scratch
and transport hardening. Current validation baseline:

- Fresh Release CMake configure: passed.
- Fresh Release full build: passed.
- Full ctest: 125/125 passed.
- MIX/KIT/DIGI GUI POD state projects through `gui_realtime_projection_v588.h`
  into compact render-friendly control and telemetry intent.
- DrSID live kick overlay base/sweep telemetry, canonical Tom note 47, complete
  8-class DrSID kit programs, and all SID-808 factory slots 120..149 are pinned
  by `drsid_808_kit_data_v589_tests.cpp`.
- Top-bar controls, dedicated DRSID panel live clock/chip/HUD/LED telemetry,
  no-adapter clearing, and DRSID knob-overlay inclusion are pinned by
  `gui_viewcontroller_wiring_v590_tests.cpp`.
- AUv3 hard-ceiling render scratch, interleaved scratch epoch checks, chunk
  beat math, and 8-attempt transport seqlock reads are pinned by
  `auv3_render_scratch_transport_v591_tests.cpp`.
- AUv2 installed component smoke and strict verifier/auval were already
  green from the v582 binary pass.
