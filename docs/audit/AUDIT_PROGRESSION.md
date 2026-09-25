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


# ArpSID 0.0.605 - Audit Progression Ledger

## Current package state

This tree is the v605 audit-closure source with pass-1 through pass-6 fixes applied.

Current discovered CTest tests on Linux CMake configure: 143.

Recent targeted validation covered the production paths changed by the audit fixes:

- DR-SID canonical register-program runner
- SID-808 engine/router bridge
- C64 PSID bridge transaction / play budget / play jam / RSID exactness
- DIGI sampler engine and sample bank
- KIT state blob and step grid
- GUI realtime projection
- Drum base coherence
- DR808 voice/accent/hat-choke/determinism/headroom loop tests

Historical skive ledger follows below for traceability.

| Skive | Audit refs | What landed |
|---|---|---|
| **v605** PHI2 interval-render integration closure | C64/PHI2/SID timing-order audit | Closed the concrete out-of-order/mistimed production-render chain from the v604 audit. AUv3 PSID loading now enables the production-owned PHI2 machine automatically for RSID (`player->enablePhi2Machine(player->isRsid())`), so PHI2 RSID attempts no longer silently stay on the instruction-atomic legacy path when PHI2 mode is requested by file type. The C64 render bridge now converts PHI2 writes into the host sample's fixed-point PHI2 interval instead of rounding writes directly to a sample-boundary bucket; each write carries `cycleInSample`, sort order is `(sample, cycle, ordinal)`, and audible SID rendering uses `queueSubphaseWrite()` plus `renderIntervalAccurate()` per sample so gate/frequency/filter/digi writes split the sample at the recorded PHI2 position. `MemoryMatrix` now accepts CIA1/CIA2/VIC device attachments, routes CPU reads/writes for `$D000-$D3FF`, `$DC00-$DDFF`, and SID ranges through those devices, and supports PSID multi-SID base routing with encoded chip/register values while preserving default `$D400-$D7FF` mirror behavior for single-SID mode. `C64Phi2Machine` now owns CIA1, CIA2, and VIC-II devices, ticks them every PHI2, wires CIA/VIC IRQ and CIA2 NMI into the CPU, exposes VIC BA/AEC/RDY ownership in the bus phase, counts VIC-stolen cycles, decays open bus, and reattaches the mapped devices whenever the SID sink is changed. `C64PsidRuntime` now mirrors loaded PSID multi-SID bases into the PHI2 memory matrix during reset. `C64SidBridgeState` now preserves per-chip register banks and timed-write chip indices, while the current single-SID audio render path explicitly skips non-primary SID writes instead of collapsing them into chip 0. GUI/source wiring tests pin the production PHI2 enable call, absence of the old rounded sample-boundary expression in the AUv3 C64 render path, interval-accurate SID rendering, PHI2 CIA/VIC mappings, multi-SID configuration, bridge chip preservation, and VIC-owned bus phase publication. Historical note: older run reported 130/130; current pass18 discovery is 146 tests. |
| **v604** PHI2 timing/order production-gate closure | C64/PHI2/SID timing audit | Closed the actionable v603 PHI2 timing-order audit without pretending the partial microcore is complete. `C64PsidRuntime` now owns a `C64Phi2Machine` behind an explicit runtime feature flag, mirrors loaded PSID RAM and post-init CPU/processor-port state into the PHI2 machine, exposes `runRsidPhi2MachineCycles()`, and forwards PHI2-timestamped SID writes into the normal runtime SID sink. The legacy RSID path remains default until full opcode/VIC/CIA parity is validated, but the new route is a real production-owned path rather than test-only scaffold. `Cpu6510Micro` now tracks per-opcode unsupported counts, last unsupported opcode, and total unsupported count; unsupported opcodes fail closed by jamming the PHI2 route instead of silently dummy-reading and finishing as success. `C64Phi2Machine` now advances open-bus decay on each PHI2 tick and publishes unsupported-opcode diagnostics alongside SID write/reset counters. `MemoryMatrix` now drives the open-bus latch with the current PHI2 timestamp for CPU reads/writes. `c64_psid_vbi_cycles_v576_tests.cpp` now uses explicit `std::max<uint64_t>` forms so GCC/Linux builds no longer fail on mixed unsigned-width arguments. `psid_rsid_c64_runtime_v261_tests.cpp` now pins the feature flag, RAM mirroring, supported LDA/STA PHI2 SID write forwarding, diagnostics, and unsupported-CLI fail-closed behavior. Historical note: older run reported 130/130; current pass18 discovery is 146 tests. |
| **v603** Full feature enablement + telemetry default closure | GUI+AUv2+settings+validation | `SettingsPanelModel` now defaults `drumEngineRouterOptIn=1` and `diagnosticDashboardEnabled=1`, with sanitize fallback to those production defaults for corrupted boolean values. AUv2 `useDrumEngineRouter_` fallback default is now `true`, so the SID-808 split-engine bridge is active by default whenever the bridge identity is SID-808; explicit OFF still disables it for compatibility checks. SETTINGS tab copy now describes an enable switch instead of an opt-in path. The C64 STATE diagnostic dashboard toggle now has real GUI behavior: ON builds and refreshes the 24-counter panel, OFF shows a compact disabled row and clears the value-label array. `gui_viewcontroller_wiring_v590_tests.cpp`, settings persistence/model tests, AUv2 bridge lifecycle tests, and divert-gate tests were updated to pin the production default and the explicit-disable path. Full `ctest` passed 130/130, AUv2 installed, AU/Logic caches cleared, strict component verification passed, and v603 source/component zips were generated. |
| **v591** AUv3 render-scratch + transport closure | render+realtime+transport | Closed the concrete Phase 2 render-path audit items without a broad preset-system rewrite. `ArpSIDAudioUnit.mm` now reserves planar render scratch at the hard frame ceiling via `ArpSIDScratchStableRenderFrames()` during init and every lifecycle refresh, so `setMaximumFramesToRender`, allocation, and standalone/AUv2 sync no longer size scratch to transient host max-frame values that could reallocate under an already-returned `internalRenderBlock`. The interleaved render path now captures `scratchEpochAtEntry`, refuses scratch use when an odd resize epoch is visible, validates both `planarL` and `planarR`, and rechecks `scratchEpochAtExit` before copying planar scratch back to the host buffer. If the epoch changes, render fails closed with silence and increments the AUv3 scratch-under-capacity diagnostic counter instead of touching stale scratch. Chunk beat positions now assign from `transport.beatPosition + chunkStart * beatsPerSample`, making the block origin explicit and avoiding future incremental-drift regressions. AUv3 and shared `HostTransportSnapshotSeqlock` coherent reads now retry 8 times instead of the old 3-attempt budget. New `auv3_render_scratch_transport_v591_tests.cpp` pins the scratch capacity helper, init-before-render reservation, lifecycle refresh behavior, interleaved epoch checks, both-buffer validation, chunk beat math, and the wider seqlock retry budget. |
| **v590** GUI/top-bar + SID-808 factory-default closure | GUI+correctness+factory data | Closed the full top-bar, DRSID tab, and SID-808 defaulting follow-up. `ArpSIDViewController.mm` now mounts the already-wired `_modelPop`, `_presetPop`, `_modePop`, and `_playModeSeg` controls into the root view, so the top bar is visible instead of silently orphaned. The dedicated DRSID tab now owns direct panel ivars for clock/chip/HUD labels and SYNC/MIDI/SID/RT LEDs; `_poll` updates those labels from live telemetry when `ArpSIDTabDrsid` is active and clears them on adapter/telemetry loss. `ArpSIDTabDrsid` is also included in realtime knob-overlay demand, so its registered knobs receive glow/modulation overlays like SEQ. `factory_patch_params.h` now treats SID-808 factory slots as the canonical absolute range `120..149` with no 127 clamp-back, repeats the five authored x0x default families through `(slot - 120) % 5`, reapplies SID-808 defaults using the original requested slot after normalized factory-slot fallback, and writes the authoritative 16-step GM/x0x coverage unconditionally so wrapped slots cannot retain stale non-drum sequencer data. `authentic_bass_sid808_logic_v241_tests.cpp` pins the `120..149` contract, no-clamp behavior, slot-family wrapping, and post-normalization slot 149 SID-808 defaults. New `gui_viewcontroller_wiring_v590_tests.cpp` pins top-bar insertion, DRSID panel ivar wiring, no stale identifier-based labels, poll-loop update/clear behavior, and DRSID realtime-overlay inclusion. |
| **v589** DrSID/SID-808 live drum-data closure | correctness+kit data | Closed the follow-up audit on the v587 drum fixes. `drsid_engine.h` now exposes live kick overlay base/sweep telemetry helpers and `analogKickSweepHz_()` is locked to the exact SID-core start delta with no accent-dependent drift: X0X8 `121+90t`, Auth `108+88t`, so `base+sweep` remains equal to the SID start Hz across tune and accent values. `drsid_instrument_program.h` now ships complete canonical DrSID kit data for all 8 GM drum classes (Kick, Snare, ClosedHat, OpenHat, Clap, Cowbell, Tom, Rim) plus `makeCanonicalDrSidProgram()` and `makeCanonicalDrSidKitPrograms()`, so KIT/engine compile paths no longer have exemplar-only gaps. `factory_sid808_kits.h` now covers the full canonical SID-808 absolute slot range `120..149`; the five authored kit families repeat deterministically across all 30 slots through `factorySid808KitFamilyIndex()`, and slot/name lookup no longer stops at 124. `drum_engine_host_bridge.h` comments now match the `120..149` SID-808 range. Regression coverage strengthened in `drum_base_coherence_v587_tests.cpp`, `drsid_instrument_program_v523_tests.cpp`, and `factory_sid808_kits_and_wavetable_runner_v536_tests.cpp`, with new `drsid_808_kit_data_v589_tests.cpp`: live kick base/sweep coherence across modes/tune/accent; Tom note 47 pinned in both engine and KIT tables; full 8-class DrSID kit compiles/fingerprints/finds each class; every SID-808 slot `120..149` routes, names, applies to `Sid808Engine`, and has audible kick data; KIT default assignments resolve to valid DrSID/SID-808/Digi concrete data. |
| **v588** GUI realtime projection closure | GUI+realtime+telemetry | Added `include/arpsid/gui/gui_realtime_projection_v588.h`, a compact trivially-copyable projection that turns GUI-owned MIX/KIT/DIGI POD state into render-friendly control intent without allocation, locks, ObjC, strings, or dynamic dispatch. MIX projection collapses solo/mute/enabled state, pan, channel/master gain, delay/reverb sends, limiter state, and active FX counts. KIT projection maps every drum class to canonical MIDI note, DrSID/SID-808/Digi absolute factory slots, active step velocity, SID-808 waveform/ADSR/PW/flags, and Digi assignment controls; Tom remains pinned to canonical note 47 from v587. DIGI projection maps active sample slots, source type, factory absolute slot, tune/start/length/volume/flags, step velocity, and active-slot count. Unified telemetry reports active mix channels, kit steps, digi slots, total GUI events, solo, limiter, active KIT target, and active DIGI slot. `gui_realtime_projection_v588_tests.cpp`: 6 sections (layout/triviality/default MIX projection; MIX solo/pan/send/FX; KIT Tom step + SID-808 voice + Digi assignment projection; DIGI sample-step projection; unified telemetry counts + step wrapping; sanitize-before-projection for corrupt restored GUI state). |
| **v587** Drum base-frequency and canonical-note coherence audit | correctness | Five bugs found and fixed during full drum/kit/808 base audit. **Bug 1** — `analogKickBaseHz_()` in `drsid_engine.h`: overlay kick base/target pitch range was 4× too narrow (`AnalogX0X8: 34+14t`, `Auth: 40+16t`) vs the SID-core `freqReg` formula (`35+58t` / `42+74t`), producing up to a full-octave divergence between the SID carrier and the overlay sine at high tune values in AnalogX0X8 mode. Fixed to match `freqReg` exactly. **Bug 2** — `analogKickSweepHz_()` in `drsid_engine.h`: sweep-delta was miscalibrated (`X0X8: 132+42t`, `Auth: 138+48t`), so `base + sweep ≠ SID startHz`. The overlay's pitch-sweep start point diverged from the SID core's own sweep-start by up to 82 Hz at tune=1.0. Fixed by deriving the deltas from `SID_startHz − SID_freqHz`: `X0X8: 121+90t` (so `(35+58t)+(121+90t)=156+148t` ✓), `Auth: 108+88t` (so `(42+74t)+(108+88t)=150+162t` ✓). **Bug 3** — `trigger(DrumType::Tom)` in `drsid_engine.h`: hard-coded note `45` passed to `triggerTom()` while `registerDrumActivity_` (overlay path) already used `canonicalMidiNoteForDrumType(Tom)=47`. The SID core used `tomBaseHzForGMNote_(45,tune)=116+58t Hz` while the overlay sine used note 47 → `138+70t Hz` — a ~19% pitch mismatch at tune=0.5. Fixed to `canonicalMidiNoteForDrumType(DrumType::Tom)` (47) for both paths. **Bug 4** — `triggerOpenHat()` in `drsid_engine.h`: used flat `4700+4400t Hz` with no per-chip compensation; `triggerClosedHat()` already applied `on6581 ? (5700+4200t) : (5300+3900t)`. Open-hat now applies matching per-chip compensation: `on6581 ? (5000+4700t) : (4700+4400t)`. **Bug 5** — `kKitDrumClassMidiNote[Tom]` in `kit_panel_model.h`: was `41` (Low Floor Tom), inconsistent with `canonicalMidiNoteForDrumType(Tom)=47` (High Mid Tom) — any code routing through the kit panel's MIDI-note table and the engine's pitch lookup simultaneously would compute different Tom pitches. Fixed to `47u`; enum doxygen comment updated to match. `drum_base_coherence_v587_tests.cpp`: 6 sections (Bug1 formula invariant — overlay base == SID freqHz at 5 tune values, regression guard confirms old formula was >40 Hz wrong at tune=1; Bug2 sweep coherence — `base+sweep=SID_startHz` at 6 tune values for both modes, regression guard confirms old formula was >70 Hz wrong; Bug3 canonical note — static check `canonicalMidiNoteForDrumType(Tom)==47`, note-47 vs note-45 pitch difference >10 Hz at tune=0.5, live Tom render audible/finite/decay; Bug4 chip compensation — 6581 and 8580 produce measurably different OpenHat output, both audible and decay; Bug5 — `static_assert kKitDrumClassMidiNote[Tom]==47`, runtime cross-check vs engine canonical, all 9 entries match expected values and valid GM range; kick render sanity at 5 tune values in both X0X8 and SidAuthentic modes). 121/121 ctest passing. |
| **v586** Tab tooltip coverage audit fix | GUI correctness | `source/au3/ArpSIDViewController.mm` `ArpSIDTabTooltipForMode()`: four bugs found during full GUI/tab pass.  **(a)** Default (Hybrid) block case 4 (`ArpSIDTabSeq=4`) returned `"DRSID tab. Dedicated drum-machine controls…"` — the copy belonged to tab 13 (`ArpSIDTabDrsid`), not the SEQ tab; in Hybrid flavor with modeIndex 0/1, hovering the SEQ tab segment showed the DRSID description.  Fixed to `"SEQ tab. Global step-sequencer and song-mode surface with swing, accent, and pattern chaining."` **(b)** Instrument block: cases 11 (`ArpSIDTabC64`) and 12 (`ArpSIDTabHiFi`) absent — both tabs are present in `kArpSIDInstrumentVisibleTabs`; hover returned `"Navigation tab"`.  **(c)** Sid808 block: same plus missing case 13 (`ArpSIDTabDrsid`), which is at index 1 in `kArpSIDSid808VisibleTabs`.  **(d)** DrumMachine block: same as Sid808.  **(e)** Default block: missing cases 11, 12, 13; after fixing case 4 the three legacy non-new tabs still fell to `default: return @"Navigation tab"`.  Fix: added cases 11+12 to Instrument block; 11+12+13 to Sid808 and DrumMachine blocks; fixed case 4 and added 11+12+13 to the default block.  `tab_tooltip_coverage_v586_tests.cpp`: 8 sections (plain C++ simulation — no ObjC/AUv3 linkage): Instrument visible tabs all non-Navigation; Sid808 visible tabs all non-Navigation + 11/12/13 explicitly checked; DrumMachine visible tabs all non-Navigation + 11/12/13; Hybrid visible tabs all non-Navigation + 11/12/13; default case 4 == kSimTooltip_Seq, != kSimTooltip_Drsid; DRSID (13) and SEQ (4) distinct codes in all three flavors that expose both; modeIndex==2 matches DrumMachine block for all covered tabs; all enum values 0–18 except legacy SidProjection (2) resolve to a real tooltip in the Hybrid/default path. 120/120 ctest passing. |
| **v585** PSID runtime scoped-sink fix | correctness | `include/arpsid/core/c64_platform.h` `runCycles()` and `executeInstruction()`: both methods permanently replaced `sidSink_` when a non-null `sid` was passed (`if (sid) sidSink_ = sid`), silently leaking any temporary override into subsequent SID write callbacks.  Any caller that needed a temporary routing (e.g. per-instruction tracing, test sink injection) inadvertently changed the platform's permanent SID sink.  Fix: save `SidRegisterSink* const prev = sidSink_` before the override and restore `if (sid) sidSink_ = prev` on return.  `attachSid()` retains unconditional permanent semantics.  `c64_psid_scoped_sink_v585_tests.cpp`: 7 sections (pre-fix leaks: sidSink_ remains as temporary after return; v585 fix: sidSink_ restored to permanent after return; null sid leaves sidSink_ unchanged; temporary sink receives writes during call only; multiple consecutive calls each restore independently; jammed passive advance still restores sink; attachSid() is unconditionally permanent). 119/119 ctest passing. |
| **v584** PSID block-start timed-write storage clear | correctness | `source/au3/ArpSIDDSPKernel.hpp` `renderC64PsidBlockIfActive_()`: `c64SidBridge_.resetTimedWrites()` cleared `timedWriteCount` and `timedWriteOverflow` but left the 4096-entry `timedWrites[]` array populated with data from the previous block.  The render loop was safe (it iterates only up to `timedWriteCount`), but diagnostic consumers and the v583 BridgeTransaction rollback could observe abandoned writes from the prior block at indices beyond the restored count.  Fix: add `c64SidBridge_.timedWrites = {}` immediately after `resetTimedWrites()` at block start.  The `regs[]` register mirror is preserved — it tracks the last audible SID state and must survive block boundaries for read-back and init-seed correctness.  `c64_psid_block_timedwrite_clear_v584_tests.cpp`: 5 sections (reset-alone leaves stale array data; block-start clear zeroes all entries including phi2Cycle; regs[] preserved across boundary; new-block writes land at index 0 cleanly; BridgeTransaction rollback on a clean block is a no-op — all stale entries are already zero). 118/118 ctest passing. |
| **v583** PSID bridge transaction (VBI + CIA) | correctness | `source/au3/ArpSIDDSPKernel.hpp` `renderC64PsidBlockIfActive_()`: a PSID play routine that failed mid-execution (instruction budget exhausted, runaway loop, malformed tune) committed half-updated SID state to the bridge before v583.  Missing gate-offs, wrong frequency or ADSR transitions, and incomplete arpeggio steps were sent to `sreg_()` as a partial frame, producing audible pops, phantom gate-ons that sustained forever, or notes from the wrong arpeggio step.  Fix: added `BridgeTransactionSnapshot` (36 bytes: `timedWriteCount` + `regs[]`) with `beginBridgeTransaction_()` and `rollbackBridgeTransaction_()` helpers.  On rollback, stale `timedWrites[]` entries beyond the restored count are zeroed (for telemetry cleanliness).  VBI path: the `beginBridgeTransaction_()` / `rollbackBridgeTransaction_()` + `jammed=true` pattern wraps `player->runPlay()`.  CIA path: same pattern wraps `player->runPsidCiaPlaybackServiceTicks()` with the additional condition that rollback fires only when `playAddressEntered && !ciaAckObserved` (play started but CIA IRQ not acknowledged — incomplete); when `!playAddressEntered` (CIA timer hasn't fired) no writes occurred and no rollback is needed.  `c64_psid_bridge_transaction_v583_tests.cpp`: 10 sections (successful play commits; failed play rolls back count+regs; stale entries zeroed on rollback; CIA commits when playAddressEntered&&ciaAckObserved; CIA rolls back when playAddressEntered&&!ciaAckObserved; no rollback when !playAddressEntered; multi-play block: second-play rollback restores first-play entries; overflow-safe rollback of max-count entries; beginTx is non-mutating; ADSR/gate integrity preserved across failed play). 117/117 ctest passing. |
| **v582** GUI/status cleanup + AUv2 installed validation closure | GUI+AUv2 | Cleaned stale scaffold-era GUI/tab text after the v581 all-tabs-implemented promotion; `tab_architecture.h` and the stale v543/v546 tab tests now consistently report 9 implemented / 0 scaffold. Added missing default-action tooltips for C64 loader/subtune/eject/unload controls, SIDCORE realtime toggles, and C64 debug filter/copy/export/pause buttons. `ArpSIDAUv2Component.mm`: fixed stopped-host `PresentPreset` handling so non-moving host preset writes are accepted as real selection intent while moving-transport mismatches remain stale replay; reset/readback now preserves selected preset 46, and AUv2 immediately mirrors accepted factory-root presentation parameters so drum-role preset 116 reports DrSID mode before the next render slice. Installed AUv2 smoke test passed through reset, GM drum promotion, DrSID preset projection, 96k/192k reconfigure, ClassInfo round-trip, and preset bridge stress. Final AUv2 component built, signed, installed, AU/Logic caches cleared, strict verifier/auval passed for all shipped subtypes, and `ArpSID_AUv2_v444_v582_full_pass.component.zip` was produced. 116/116 ctest still passing from this full pass. |
| **v581** C64 STATE final GUI promotion | GUI | `include/arpsid/gui/tab_architecture.h`: promoted `C64STATE` from `Scaffold` to `Implemented`, raising the architecture count to 9 implemented / 0 scaffold. `ArpSIDViewController.mm`: replaced the counter-only C64 STATE dashboard with an independent scrollable read-only CPU/SID/CIA/memory inspector: platform/clock/CPU registers, SID model/voice/topology readout, PSID load/init/play map, CIA/IEC/tape/ROM state, SID register mirror, memory-window hash/change summary, two live C64 bus oscilloscope views, SIDCORE write timeline, and the existing 23 audit counters. The panel now owns separate C64 STATE labels/scopes/timeline pointers so it no longer steals the legacy C64 cockpit iVars. `_poll` now requests C64 heavy telemetry for the C64 STATE tab, updates the inspector, refreshes its timeline, and clears its scopes on bridge loss. `C64ChipSnapshot` + `ArpSIDTelemetry` now expose PSID load/init/play addresses and active SID base topology for GUI display. Updated stale v543/v546/v562/v566 tab-architecture tests to 9/0, and added `c64_state_tab_promotion_v581_tests.cpp` (all tabs implemented + C64 telemetry snapshot carries PSID address/topology data). 116/116 ctest passing. |
| **v580** C64 PSID SIDCORE timeline + shadow fix | GUI+correctness | `source/au3/ArpSIDDSPKernel.hpp`: fixed the C64/PSID early-return path bypassing the v550 SIDCORE end-of-block publication.  Pre-fix, C64 register-write events used `sidCoreBlockIndex_ * 512 + sampleOffset`, but C64 returned before `++sidCoreBlockIndex_`, so every PSID block reused the same 0..511 stamp window forever.  The fixed path adds render-thread SIDCORE timeline state (`sidCoreSampleCursor_`, `sidCoreBlockSampleBase_`), begins each real render block from the current sample cursor, computes write stamps as `blockSampleBase + sampleOffset`, and advances the cursor in `finishSidCoreBlockTimeline_()` before both normal and active-C64 returns.  This also removes the hard-coded 512-sample stride for normal direct SID writes, so 64/1024/4096-frame hosts no longer produce backwards timeline stamps.  Active C64 now publishes the live SIDCORE snapshot before returning; idle C64 advances the cursor without publishing stale snapshots.  C64 handoff seeding and timed play-routine writes now mirror into `sidQueuedShadow_` after `publishSidCoreRegWrite_()`, preserving previous-shadow gate/filter flag derivation while making SIDCORE live snapshots reflect the PSID audio authority (freq/control/ADSR/volume).  Timed-write sample/cycle provenance is carried through the insertion sort and cycle offsets are clamped to `uint16_t` for the shadow.  `c64_psid_sidcore_timeline_v580_tests.cpp`: 9 sections (old C64 early-return repeated stamps; v580 C64 active blocks advance to 128/640; variable block sizes 1024→64→4096 remain monotonic vs old fixed-512 regression; idle advances cursor without snapshot; handoff seeds writeable shadow regs and skips read-only 0x19–0x1C; gate flags are derived before shadow update; cycle provenance clamps at 0xFFFF; live snapshot reflects C64 shadow after first play writes; timeline saturates instead of wrapping). 115/115 ctest passing. |
| **v579** C64 PSID init-register seeding fix | correctness | `source/au3/ArpSIDDSPKernel.hpp` `drainPendingPsidHandoff_()`: added init-register seeding loop immediately after `c64SidBridgeInstallWithSink()`.  Root cause of ordinary PSID choppiness after v576+v577+v578: the PSID init routine runs off-thread inside `C64Runtime::runInit()` → `runBooted_()` → `platform_.executeInstruction()` while `sidSink_` points to `C64RuntimeSidSink` (telemetry only, no audio engine writes).  `c64SidBridge_.reset()` zeroes the bridge register image; `c64SidBridgeInstallWithSink()` installs the bridge with `deferEngineWrites=true`.  At this point `sreg_()` has ADSR=0 for all three voices.  With SID ADSR=0: attack=0 (instantaneous), decay=0 (instantaneous), sustain=0 (zero level), release=0 — the envelope collapses to amplitude 0 within one sample after gate-on.  Every note at 50 Hz VBI rate produces a brief click then silence → continuous choppiness for any ordinary PSID tune (Rob Hubbard, Jeroen Tel, Last Ninja, Commando, etc.) that sets ADSR once in init and never re-writes it during play.  The volume register ($D418) was also zeroed, which would have silenced output entirely if play never wrote it.  `platform().sidRegisterImage()` (`C64Platform::sidRegs_[]`) is authoritative: `writeMapped_()` stores every SID write to `sidRegs_[r]` unconditionally regardless of which `sidSink_` is active, so after `runInit()` completes the array holds the complete post-init register state.  Fix: after `c64SidBridgeInstallWithSink()`, loop `r = 0..31`, and for each `c64SidRegWriteable(r)` register call `sreg_().write(r, initRegs[r])` (direct call, bypasses bridge timed-write buffer — no PlayBase contamination) and assign `c64SidBridge_.regs[r] = initRegs[r]` (seeds bridge read-back).  Read-only registers 0x19–0x1C (POTX, POTY, OSC3/RANDOM, ENV3) are skipped; 28 writable registers are seeded.  The first play call at sample 0 immediately overwrites per-frame registers (freq hi/lo, gate/waveform, volume) with frame-0 values — init-state values are never audible as a stale frame.  `c64_psid_init_reg_seed_v579_tests.cpp`: 12 sections (read-only window 0x19–0x1C covers exactly 4 regs / 28 writeable; ADSR=0 produces zero-sustain → silence model; unseeded engine ADSR=0 confirmed; seeded engine ADSR matches platform image; seeding skips read-only regs; bridge regs seeded from platform; platform image always updated regardless of sidSink; play routine does not re-write ADSR per frame; loop count: 28 seeded + 4 skipped = 32; first play overwrites per-frame regs, ADSR preserved from seeding; choppiness rate model: ADSR=0 → 50 silent notes/s → choppy, seeded → audible sustain; volume reg 0x18 writeable and seeded). 114/114 ctest passing. |
| **v578** C64 PSID full-VBI passive advancement fix | correctness | `source/au3/ArpSIDDSPKernel.hpp` `renderC64PsidBlockIfActive_()` VBI path: the passive cycle debt model was the root cause of remaining choppiness after v576+v577.  The debt model accumulates at most ~one audio block's worth of C64 cycles at any moment.  When two VBI play calls fire within the same audio block (common with 1024-sample blocks at 44.1 kHz PAL where one block spans 1.17 VBI periods) the first play call consumes 19656 cycles of debt; the second receives only `22879 − 19656 = 3223` cycles (16.4% of one VBI).  For 512-sample blocks at consecutive block boundaries, each play gets only `11440` cycles (58.2%).  With only 3223 passive cycles, CIA Timer A advances 3223/19705 = 16.4% of its period — entering the play routine with the timer 16482 cycles from fire (84% phase error) instead of the correct 49 cycles.  Any tune using CIA hardware timers for arpeggio step timing, vibrato rate, or sub-frame sequencing inside the play routine reads the wrong phase → wrong arpeggio steps → choppy.  Fix: unconditionally call `platform.runCycles(playPhi2Cycles, &c64SidBridge_)` then reduce debt by `min(debt, playPhi2Cycles)`.  Every play now gets exactly one full PAL/NTSC VBI frame of passive advancement regardless of block size, play density, or debt state:  CIA Timer A fires exactly once per play entry (residual 49 PAL / 50 NTSC cycles = < 0.3% of VBI period); VIC raster and NMI state advance a full VBI between plays.  Audio timing unaffected: SID write offsets are computed as `write.phi2Cycle − PlayBase.cycle` (a delta); absolute phi2Cycle magnitude never enters sample-mapping.  C64 clock may temporarily run up to one VBI ahead of audio time; steady-state drift: `+3 cycles/play × 50 plays/s = 150 cycles/s` (0.015% at PAL 44.1 kHz), absorbed by the 8×VBI debt cap.  `c64_psid_full_vbi_passive_v578_tests.cpp`: 9 sections (1024-block second-play shortfall < 20% of VBI, old bug confirmed; fix: both plays advance full 19656 cycles; 512-block consecutive plays get 58% VBI old vs 100% fix; CIA residual 49 cycles after 19656 passive, correct entry phase; steady-state drift < 0.02% at PAL 44.1 kHz; C64 overshoot bounded < one VBI period; SID write deltas unchanged by absolute phi2Cycle; CIA service path unaffected: 21753 cycles unchanged). 113/113 ctest passing. |
| **v577** C64 PSID play jam + instruction budget fix | correctness | `source/au3/ArpSIDDSPKernel.hpp` `renderC64PsidBlockIfActive_()` VBI path: two compounding bugs caused remaining choppiness after v576.  **Bug A — CPU continuation during passive**: when `player->runPlay(kC64PsidMaxInstructionsPerPlay)` exhausted its budget it returned `false`, leaving `jammed=false` and the 6510 PC mid-play.  On the next audio block the passive `platform.runCycles(playPhi2Cycles, &c64SidBridge_)` inadvertently continued executing the remainder of the play routine.  Those SID writes had `phi2Cycle < PlayBase.cycle` (the base was recorded after passive), so the sample-offset guard (`deltaCycle ≤ 0 → playSample`) mapped them all to `playSample` — a burst of note events at the block boundary, creating a spurious double-update.  Then the explicit `runPlay()` re-executed from scratch with contaminated machine state: arpeggio/vibrato table indices had already been advanced by the passive continuation, so the re-run started from the wrong arp/vibrato frame.  Result: one arp step repeated (e.g. C→E→G→G→C→E instead of C→E→G→C→E→G), audible as a missed arpeggio step every 20 ms.  Fix: `platform.cpu().state().jammed = true;` immediately before `platform.runCycles(catchup, &c64SidBridge_)` in the VBI else-branch.  With `jammed=true`, `stepPhi2_()` calls `cpu_.advance(1)` as a no-op: CIA/VIC timers still tick correctly, but no 6502 instructions execute.  `bootFromResetVectorPreservingMachine()` inside `runPlay()` unconditionally clears `jammed` so the explicit play call runs normally.  **Bug B — Instruction budget 4096 too low**: `kC64PsidMaxInstructionsPerPlay = 4096` was insufficient for complex tunes; Rob Hubbard / Jeroen Tel routines require up to 8000+ instructions/play, triggering Bug A every VBI frame for those tunes.  Fix: raise constant from 4096 to 16384 (4×).  CPU cost: 16384 × 15 ns × 50 plays/s = 12.3 ms/s — well within real-time budget.  CIA path unaffected: it uses `runPsidCiaPlaybackServiceTicks()` which manages its own CPU loop and does not touch `kC64PsidMaxInstructionsPerPlay`.  `c64_psid_play_jam_v577_tests.cpp`: 9 sections (budget 16384 covers all listed tunes including Bionic Commando 8100 instr; old budget 4096 fails for tunes >4096; budget hit leaves `jammed=false` and PC mid-play — bug confirmed; passive with `jammed=true` executes 0 CPU instructions / passive with `jammed=false` executes play continuation — bug confirmed; passive SID writes with `phi2Cycle < base.cycle` all clamp to `playSample`; arp-pointer contamination: passive continuation advances arp index → explicit play starts at wrong frame; CPU cost 16384×15ns×50/s < 15 ms/s; `jammed=true` before passive cleared by `bootFromResetVector` inside `runPlay()`; operation order jam→passive→base→play verified; CIA path: catchup formula = 21753 unchanged by v577). 112/112 ctest passing. |
| **v576** C64 PSID VBI passive cycle budget fix | correctness | `source/au3/ArpSIDDSPKernel.hpp` `renderC64PsidBlockIfActive_()` VBI path: passive PHI2 advancement was capped at `kC64PsidMaxPassiveCatchupCyclesPerPlay = 8192` cycles per play call.  PAL VBI frame = 19656 cycles.  8192/19656 = 41.7%.  Between consecutive `runPlay()` invocations, CIA Timer A and the VIC raster counter were only advanced 41.7% of a real VBI period — on a real C64 these advance by exactly one full VBI frame between play() calls.  CIA Timer A default latch = 19705 cycles (round(985248/50)); with only 8192 passive cycles the timer was 11513 cycles from its next fire (58% of a period away) instead of the correct ~49 cycles (0.25%).  Any tune using CIA Timer A for arpeggios, vibrato, or sub-VBI tempo sequencing inside the play routine saw the timer at completely the wrong phase, producing wrong arpeggio rates, missed timer events, or vibrato at 41.7% speed.  Additionally, the undershoot caused the passive debt to grow by ~11464 cycles per VBI (debt added = 19656, consumed = 8192), hitting the 8×VBI cap (157248 cycles) after ~14 VBIs (≈0.28 seconds) — at which point excess cycles were silently discarded and the C64 clock permanently drifted from the audio clock.  Fix: replace `kC64PsidMaxPassiveCatchupCyclesPerPlay` with `playPhi2Cycles` (19656 PAL / 17095 NTSC) as the VBI passive catchup cap: `const uint64_t catchup = std::min(c64PsidPassiveCycleDebt_, playPhi2Cycles)`.  The C64 now advances exactly one VBI frame of cycles before each play call: CIA Timer A completes its period (residual 49 cycles from fire = <0.25% error); VIC raster position at play entry matches a real VBI interrupt; passive debt is stable (add ≈ consume ≈ 19656 cycles per VBI); debt cap never reached in normal operation.  CIA path unaffected — it already uses `max(playPhi2Cycles+2048, 8192) = 21753` which is always > `playPhi2Cycles`.  `c64_psid_vbi_cycles_v576_tests.cpp`: 9 sections (PAL/NTSC VBI constants 19656/17095 both > 8192; old cap fractions 41.7%/47.9% both < 50%; full-period CIA alignment: residual 49 cycles / <1% vs pre-fix 58% error; debt arithmetic: add≈consume≈19656 per VBI, net±2 cycles; pre-fix debt growth: surplus 11464 cycles/VBI, cap hit in <20 VBIs; post-fix debt stability: 20-VBI simulation stays < one VBI period; CIA timer phase: post-fix <100 cycles from fire vs pre-fix >1000 cycles; CIA path unchanged: cap formula always = 21753 regardless of old constant; NTSC variant: 17095 cycles, residual 50 cycles, <1% error). 111/111 ctest passing. |
| **v575** C64 PSID VBI PlayBase timing fix | correctness | `source/au3/ArpSIDDSPKernel.hpp` `renderC64PsidBlockIfActive_()` VBI path: `PlayBase` was recorded BEFORE the `platform.runCycles(catchup=8192)` passive PHI2 advancement, displacing every VBI SID write by `kC64PsidMaxPassiveCatchupCyclesPerPlay = 8192` PAL cycles.  At 44.1 kHz PAL this equals `round(8192 × 44100/985248) = 367` audio samples = 8.3 ms.  All SID register writes from `player->runPlay()` carried a `+367`-sample offset relative to the block's play-fire position: notes started and stopped 8.3 ms late on every VBI frame.  When `playSample + 367 > numFrames − 1`, writes were clamped to the last sample of the block → inter-VBI update intervals became irregular (e.g. 656, 1024 samples instead of the correct ~880 samples at 44.1 kHz PAL) → audible choppiness regardless of the instruction budget fix in v574.  Fix: move `playBases[...] = {platform.phi2Cycle(), playSample}` to AFTER `platform.runCycles(catchup, &c64SidBridge_)` in the VBI path only.  CIA-timed tunes are unaffected: for those, `PlayBase` is correctly recorded before `runPsidCiaPlaybackServiceTicks()` (the CIA timer fire cycle is the meaningful reference, not the passive runup).  `c64_psid_playbase_timing_v575_tests.cpp`: 9 sections (passive-displacement constant: 8192 cycles == 367 samples at PAL 44.1 kHz, with 41.7% VBI-fraction check; pre-fix offset math documents 367-sample error at 5 representative playSamples; post-fix writes land at ~playSample ± 1 sample, not +367; block-boundary clamping: pre-fix clamps for playSample > 145 in a 512-sample block, post-fix doesn't; first-frame analysis: second play at sample 370 → pre-fix clamped to 511, post-fix at 371; fixed timing sweep: 10 × 4 write-offset combinations all land within 5 samples of playSample; multi-block VBI period regularity: 6-block simulation gives ~880-sample gaps, within [870, 895]; displacement formula cross-check at PAL/48 kHz/NTSC; CIA path unchanged: CIA latch 17095 > passive catchup 8192, PlayBase-before-service semantics preserved). 110/110 ctest passing. |
| **v574** C64 PSID play instruction budget fix | correctness | `source/au3/ArpSIDDSPKernel.hpp` `kC64PsidMaxInstructionsPerPlay`: raised from 2048 to 4096, matching `C64Runtime::runPlay()`'s own default parameter.  Root cause of "C64 play is still choppy": complex PSID tunes (Rob Hubbard, Jeroen Tel, etc.) have play routines requiring 3000–6000+ 6502 instructions per VBI frame.  With the 2048-instruction cap, `renderC64PsidBlockIfActive_()` called `player->runPlay(2048)` which returned `false` (budget exhausted before the routine returned).  `c64BlockPlayCalls_` is only incremented on a `true` return, but — critically — any partial SID writes that occurred before the cap are already accumulated in `c64SidBridge_` and ARE applied to `sreg_()`.  Every VBI frame therefore produced an incomplete set of SID register updates: missing gate-ons, dropped frequency and envelope writes, truncated arpeggio patterns → choppy and wrong audio on any tune with a complex play routine.  The 4096-instruction budget covers the vast majority of real-world PSID tunes; the guard against infinite-loop broken play routines is preserved.  CPU impact is negligible: 4096 × 15 ns ≈ 61 µs wall-clock, << 11.6 ms audio block deadline (189× headroom).  `c64_psid_play_budget_v574_tests.cpp`: 9 sections (budget constant == 4096 not 2048 — kernel matches C64Runtime default; simple trivial play completes within budget; medium ~2040-NOP routine completes at 4096 budget; infinite-loop guard still fires at limit; partial-play produces SID writes, not silent; complete play returns true / partial play returns false; CPU real-time budget math — 61 µs << block deadline, extra cost < 100 µs; monotone budget effect — higher budget ≥ same SID writes; budget matches C64Runtime::runPlay() default 4096). 109/109 ctest passing. |
| **v573** Output limiter state-restore fix | correctness | `source/arpsid_processor_phase2.cpp` `resetRenderModeOutputNormalizer_()`: four limiter fields (`limiterEnabled`, `limiterThreshold`, `limiterAttackMs`, `limiterReleaseMs`) and the `SimpleLimiter` object's time constants were NOT re-projected from `paramValues[]` after `setState()` / `applyCanonicalStateRoot_()`.  `reverbMix` was already correctly recovered on line 1676 of the same function; the limiter was silently absent.  After a preset restore the plugin used C++ field defaults: threshold=0.97 (correct=0.94, off by 0.03), attack=0.5 ms (correct=1.6 ms, 3.2× too fast), release=200 ms (correct=356.5 ms, 56% too short).  A custom preset setting a low threshold (e.g. 0.70) would be replaced by near-brick-wall 0.97; a slow release (901 ms) would collapse to 200 ms.  Fixed by adding four projection lines matching the transformations in `runtimePolicySetLimiter*` (sid_runtime_parameter_services.h): `limiterEnabled = paramValues[kParamOutputLimiter] > 0.5f`, `limiterThreshold = clamp(paramValues[kParamLimiterThreshold], 0.5, 1.0)`, `limiterAttackMs = clamp(paramValues[kParamLimiterAttack], 0, 1) × 20`, `limiterReleaseMs = 10 + clamp(paramValues[kParamLimiterRelease], 0, 1) × 990`, plus `limiter.setAttackMs / setReleaseMs` to update internal exponential coefficients.  `limiter_state_restore_v573_tests.cpp`: 9 sections (enabled formula >0.5 boundary, threshold clamp [0.5,1] at 6 values, attack-ms [0,20] range at 4 key values including default 1.6 ms, release-ms [10,1000] range at 4 key values including default 356.5 ms, default param alignment — all 4 fields from parameter_ids.h defaults, pre-fix stale-default documentation — all 3 discrepancies quantified, monotonicity of attack+release+threshold, clamp guards for all 4 params OOR, SimpleLimiter::setAttackMs/setReleaseMs coeff contracts at 44100 Hz with exact exp() verification). 108/108 ctest passing. |
| **v572** Sequencer step note range consistency | correctness | `source/arpsid_processor_phase2.cpp` `processSequencer()`: the MIDI note formula was `clamp(round(noteNorm × 48) + 36, 0, 127)` — a 4-octave window [36, 84] anchored at C2. The DSPKernel (AUv3) path and the UI both use `clamp(round(noteNorm × 127), 0, 127)` (full 0–127 MIDI range). At the parameter default of 0.5: Phase2 played MIDI 60 (Middle C) while AUv3 and the UI showed/played MIDI 64 (E4). At param=0.0: Phase2 played C2 (36) but AUv3 played C-1 (0) — a 3-octave gap. At param=1.0: Phase2 played C6 (84) but AUv3 played G9 (127) — a 43-semitone gap. Fixed to `clamp(round(noteNorm × 127), 0, 127)`. Middle C (MIDI 60) now correctly maps to param 60/127 ≈ 0.4724, matching the UI's `defaultCenter:60.0f/127.0f` hint. `seq_note_range_v572_tests.cpp`: 8 sections (canonical formula at 6 key values, old formula divergence — range 48 vs 127 documented, two-path consistency at 21 sample points, endpoints 0/127, default param 0.5 → MIDI 64 matching DSPKernel+UI, middle-C alignment 60/127 → MIDI 60, monotone non-decreasing, clamp guards negative/over/NaN). 107/107 ctest passing. |
| **v571** Sequencer swing tempo-preservation | correctness | `source/arpsid_processor_phase2.cpp` `processSequencer()` `stepDuration` lambda: the even-step coefficient was `seqSwing * 0.5 * 0.5` (= `seqSwing * 0.25`), so the odd+even step pair summed to `(1 + s*0.5) + (1 − s*0.25) = 2 + 0.25s` instead of 2.0 — producing up to 12.5% global tempo drift at maximum swing (s=1: pair was 2.25×). Compare the arpeggiator's symmetric formula which correctly uses `1 − swingAmt` (pair = 2.0). Fixed to the symmetric coefficient: `swingFactor = seqSwing * 0.5`, even step = `samplesPerStep * max(0.1, 1.0 − swingFactor)`, so pair = `(1 + s*0.5) + (1 − s*0.5) = 2.0` exactly at all swing values. The `max(0.1, ...)` floor ensures even step never collapses below 10% of base even if swing were driven beyond its 0–1 range by future code. `seq_swing_tempo_v571_tests.cpp`: 8 sections (pair sum=2.0 at swing=0, swing=0.5 across 3 sample rates, swing=1.0; old formula drift documented — old pair=2.25× at s=1; odd≥even at all 21 swing points; unity at swing=0 both steps exact; max swing 75/25 triplet feel with 3:1 ratio; floor guard even≥0.5× within normal range). 106/106 ctest passing. |
| **v570** Portamento time formula consistency | correctness | `include/arpsid/core/sid_runtime_synth_register_scheduler.h` `canonicalScheduleSynthModeNoteOn()`: replaced linear `portaTime × 4.0f` with canonical `ArpSID_normToPortamentoSeconds(portaTime)` = `portaTime² × 5s`. The old formula diverged from `bitperfect_engine.h::setPortamentoTime()` (which uses the shared `ArpSID_normToPortamentoSeconds` function) at every parameter value — e.g. value=0.5 gave 2.0s in synth mode vs 1.25s in normal mode (1.6× too long), and value=1.0 gave 4.0s vs 5.0s (1.25× too short). Both paths now use the quadratic `v² × 5s` formula, so portamento feels identical regardless of which rendering mode is active. `portamento_time_formula_v570_tests.cpp`: 6 sections (canonical formula v²×5s at 5 key values + agreement with `ArpSID_normToPortamentoSeconds`, old-linear-vs-canonical divergence documented at 3 key values, two-path consistency at 11 sample points, endpoints 0s/5s, monotone increase, clamp guards for negative/over/NaN inputs). 105/105 ctest passing. |
| **v569** LFO rate bulk-projection fix | correctness | `include/arpsid/core/sid_runtime_backend_projection.h` `projectRuntimeStateToBackends()`: fixed second LFO rate call site missed by v568. The bulk/force-reload path (fires on preset restore, first-apply, sample-rate change) used the old linear formula `lfo.setRate(0.1f + params[lfoR[l]] × 19.9f)` while the incremental-parameter path had already been fixed in v568. Without this fix, a preset recalled from saved state would restore a different LFO speed than what the user last heard during live editing — creating a state-restore divergence. Fixed to `lfo.setRate(0.1f × std::pow(200.0f, rv))` with explicit `std::clamp(rv, 0, 1)`. `lfo_rate_projection_v569_tests.cpp`: 6 sections (two-path consistency at 21 sample points — bit-identical output from both formulas, endpoints 0.1/20 Hz, midpoint cross-path agreement at √2 ≈ 1.414 Hz, LFO object observes correct phase increment at 44100 Hz for min/mid/max rates, monotone increase, clamp guard for out-of-range param values). 104/104 ctest passing. |
| **v568** LFO rate-curve fix | correctness | `include/arpsid/core/sid_runtime_parameter_services.h` `runtimeApplyProjectedBackendParameter()` slot==0 (LFO rate): replaced LINEAR Hz mapping (`rateHz = 0.1 + value × 19.9`) with EXPONENTIAL mapping (`rateHz = 0.1 × 200^value`). The old linear formula placed the knob midpoint at ≈10.05 Hz — above the entire vibrato/tremolo register (4–8 Hz), making the musically useful range (0.1–5 Hz) crammed into the bottom quarter of the knob. The exponential formula places the geometric midpoint at √(0.1×20) = √2 ≈ 1.414 Hz — slow vibrato / characteristic sweep territory. Endpoints unchanged: value=0 → 0.1 Hz, value=1 → 20 Hz. `LFO::setRateTempo(bpm, division)` (host-sync path) unaffected. `lfo_rate_exp_v568_tests.cpp`: 6 sections (endpoints 0.1/20 Hz with old-vs-new endpoint parity, geometric midpoint √2 ≈ 1.414 Hz with >6× improvement factor documented, monotone increase across 20 sample points, clamp guards for value<0 and value>1, setRateTempo BPM path unchanged — pins quarterAt120=2.0 Hz not 57.7 Hz, phase-increment consistency at 44100 and 48000 Hz with old midpoint > 7× faster than new). 103/103 ctest passing. |
| **v520** DrumContext separation | #5, #39, #74 | `drum_context.h` — DrumContext enum, DrumKitIdentity POD, factory-slot ranges (DrSID 80–119, SID-808 120–149, Digi 150–179), `factorySlotContextLegacy`/`factorySlotContextNew`/`factorySlotContext`, compile-time invariants pinned. |
| **v521** Render epoch + scope triple-buffer | #1, #14, #15 | `render_epoch.h` (atomic counter + RAII scope), `scope_triple_buffer.h` (wait-free SPSC). Bitperfect engine swapped from 2-buf to 3-buf. AUv2 wrapper bumps epoch on state changes. |
| **v522** SidWriteQueue sort | #10, #11 | O(n²) insertion sort → `std::sort` introsort. Capacity 32768 → 65536. SidTimedEventQueue same fix. |
| **v523** DrSidInstrumentProgram contract | DrSID §3, §4B | POD format (16-byte DrSidRegisterStep × 32), constexpr `programIsWellFormed`, 3 exemplar programs (Kick/ClosedHat/Clap). |
| **v524** AUv2 render-notify RT-safety | #2, #3 | Doubled-notify removal on pre-fail path. Paired release/acquire on (proc, userData). Per-callback violation attribution. |
| **v525** Ingress fallback edge ring | #24, #25 | `ingress_fallback_edge_ring.h` (Vyukov MPSC ring, 256 slots) for sustain/sostenuto/RPN/NRPN/transport edges. `drainPushOrDrop_` helper checks every drain push. |
| **v526** Audit follow-ups | #4, #5, slice 1/4/5 follow-up | DrumContext gate in factory-loader. DrSidKitCompiler + CRC32 fingerprint. AUv2 bridged-block grace-wait. Scratch capacity pre-render guard. |
| **v527** Forensic engine sanity | #56, #58, #59, #60, #67, #74 | Parameter block contiguity pins (static_assert). RO SID register range-check. canonical factory bank sweep (legacy versions were 128-slot). |
| **v528** AUv2/AUv3 P2 fixes | #49, #50, #51, #52, #55 | Pre-notify failure no longer doubled. Host-buffer scratch counter. AUv3 scratch-resize epoch + lock. AUv3 under-capacity silent fail-closed. Chunked render determinism math. |
| **v529** Multi-SR rendered audio | #48, #69 + multi-SR | Scoped render-use hoisted. Real SIDChip render at 44.1/48/88.2/96/176.4/192 kHz. canonicalizeHostSampleRate, c64_timing_math, host_transport sanitization. |
| **v530** SingleSidThreeVoiceEngine | #29 | `sid_voice_allocator.h` (3-slot, 4 stealing policies, choke groups). `single_sid_three_voice_engine.h` (1 chip, 3 voices, voice stealing). |
| **v531** BitPerfect DSP authenticity | #30, #34, #35, #36, #38 | Scope-averaging removal. uint64 limiter counters pinned. L/R RNG independence. Output-stage dither. ±8 filter abs-clamp. |
| **v532** Filter unification + topology mode | #29 wire-up, #31, #32 | `sid_filter_core.h` (shared 6581 nonlinearity, feedback, leak laws). BitPerfectEngine `SidChipTopologyMode` switch. |
| **v533** Zero-cycle interp + param smoothing | #33, #37 | Sub-cycle phase advance in SIDChip's zero-cycle path. `param_smoothing.h` (canonical IIR + transient detector). |
| **v534** DrSid clock + voice alloc | #41, #42, #45 | Invalid clock rejection (no silent PAL fallback). 12-slot SidVoiceAllocator on DrSidEngine. |
| **v535** Sid808Engine + DrumEngineRouter | #39, #74 | `sid808_engine.h` (clean from-scratch analog x0x engine). `drum_engine_router.h` (context-aware dispatch). |
| **v536** Factory SID-808 kits + wavetable runner | #43, #44, #74 wire | `factory_sid808_kits.h` (5 kits for slots 120-124). `drsid_wavetable_program_runner.h` (register-microprogram runner). |
| **v537** DrSid restore + host bridge | #40 | Serialized model is ground truth on restore (overlay no longer flips model). `drum_engine_host_bridge.h` (host-side reference wire-up). |
| **v538** AUv2 bridge + P2 pluck | #61, #62, #63 + bridge embed | Bridge embedded in `ArpSIDAUv2Instance`; v603 promotes the router flag to default ON with an explicit OFF switch. Unique sequencer step names (96 distinct). |
| **v539** Bridge lifecycle + PSID diag | #65, #66 + lifecycle wire | Bridge follows AUv2 lifecycle (prepare/setClockFrequency). PSID video-standard fallback counter. PSID handoff counter. |
| **v540** Bridge audio divert + README | #71, #72 + divert gate | Divert gate in `componentRender` (gated by `useDrumEngineRouter_` + SID-808 context; v603 default ON, explicit OFF supported). README versjon-drift fix + audit-progression ledger. |
| **v543** GUI tab architecture scaffold | GUI | `tab_architecture.h` — 9-tab canonical inventory (DRSID, SID-808, SEQ, DIGI, KIT, MIX, SIDCORE, C64STATE, SETTINGS), `TabImplementationStatus`, constexpr counts. |
| **v544** SETTINGS + C64STATE tab panels | GUI | `settings_panel_model.h`, `_settingsPanel_v544_:` Cocoa builder (6 NSPopUpButton sections + 2 NSButton toggles), `_c64StatePanel_v544_:` diagnostic dashboard scaffold, tab indices 14+15 wired. |
| **v545** SidCore panel model + ring | GUI | `sidcore_panel_model.h` — MPSC ring-buffer (256 slots) + triple-buffer for register-write timeline. RT-safe wait-free publish path. |
| **v546** Tab-bar wire-up | GUI | `ArpSIDTabSettingsV544=14`, `ArpSIDTabC64StateV544=15` added to enum + all 4 visible-tab arrays + labels + tooltips. |
| **v547** MIX model + SIDCORE timeline view | GUI | `mix_panel_model.h` (16-ch POD + FX chain + send buses + master, 1264-byte layout). `ArpSIDSidCoreTimelineView_v547` renders register-write timeline + per-voice snapshot. |
| **v548** MIX NSView builder + tab 16 wire-up | GUI | `_mixPanel_v547_:` Cocoa builder: scrollable 16-channel strip (vol slider + pan slider + S/M buttons + 5 FX dropdowns) + master section (vol/limiter/width/dim) + 2 send-bus controls. `ArpSIDTabMixV547=16` wired in all 4 visible-tab arrays, label/tooltip/activeChromeHostPanel/showTab. 83/83 ctest passing. |
| **v549** Live diagnostic counter feed | GUI+kernel | `diagnostic_snapshot.h` — 23-field POD snapshot (2×uint32 + 23×uint64, trivially copyable, layout-pinned). `SidRegisterEngine::filterAbsClampHitCount()` + `SidRuntimeModel::pendingEventsDrainDroppedCount()` / `ingressFallbackEdgeOverflowCount()` public accessors. `ArpSIDDSPKernel::collectDiagnosticCounters()` pulls 10 kernel-accessible counters. `ArpSIDDSPKernelAdapter (DiagnosticCounters)` category: 13 `std::atomic<uint64_t>` iVars, `storeAuv2DiagCounters:` (RT-safe push from AUv2 render thread), `storeAuv3ScratchEpoch:underCapacity:` (RT-safe push from AUv3 render block), `readDiagnosticCounters:` (GUI thread pull). `publishedAdapter` atomic published in AUv2 init/reset/close paths for lock-free render-thread adapter access. ViewController: `ArpSIDDebugAdapterLike` `@optional readDiagnosticCounters:`, `_c64DiagValueLabels_v549_` 23-element NSTextField array, `_poll` live 60 Hz refresh on `ArpSIDTabC64StateV544`. Demo data + static "—" placeholder footer removed. 84/84 ctest passing. |
| **v550** Live SIDCORE register-write feed | GUI+kernel | `ArpSIDDSPKernel`: `#include sidcore_panel_model.h`, `std::atomic<SidCorePanelModel*> sidCorePanelModel_{nullptr}`, `uint64_t sidCoreBlockIndex_` (render-thread monotonic counter), public `setSidCorePanelModel()` setter (atomic release). Private helpers: `publishSidCoreRegWrite_()` (inline, called at each of 3 `sreg_().write()` sites in processBlock — sorted-write loop line ~1112, parameter-import line ~2241, pitch-bend loop lines ~3089-3090; derives voice hint + gate/waveform/hard-restart/filter-mode flags from `sidQueuedShadow_` shadow), `publishSidCoreLiveSnapshot_()` (reads all 25 voice+filter registers from `sidQueuedShadow_.value` into `SidCoreLiveSnapshot`, called once per block after `++sidCoreBlockIndex_`). `ArpSIDDSPKernelAdapter (SidCorePanel)` category: `setSidCorePanelModel:` forwards to kernel. `ArpSIDDebugAdapterLike` `@optional setSidCorePanelModel:`. ViewController: wires `&_sidCoreModel_v547_` via adapter in `_connectBridgeInternal:` dispatch block; `_poll` calls `[_sidCoreTimelineView_v547_ refresh]` when `wantsSidCoreViz`. `sidcore_feed_v550_tests.cpp`: 7 sections (layout pins, event flag constants, ring push/drain, ring overflow, panel model round-trip, reset). 85/85 ctest passing. |
| **v551** SettingsPanelModel ↔ AU state persistence | GUI+AU | `ArpSIDDSPKernelAdapter`: `_settingsModel` iVar (32-byte POD, GUI-thread only, initialized from `makeDefaultSettings()` in `-init`). `(SettingsPersistence)` category: `getSettingsModel:` (plain copy out), `setSettingsModel:` (copy in via `sanitizeSettings` — clamps all out-of-range enums/flags before storing). `ArpSIDAudioUnit`: private `-_restoreSettingsFromStateDictionary:` helper (validates `NSData` length == 32, calls `deserializeSettings` + `setSettingsModel:`). `fullState` serializes 32-byte settings blob to `@"ArpSIDSettings_v1"` key. `setFullState:` + `setFullStateForDocument:` both call `_restoreSettingsFromStateDictionary:` after state apply — AUv2 state save/restore flows through AUv3 `fullState`/`setFullState:` automatically via `wrapClassInfoDictionary`/`unwrapClassInfoDictionary`. ViewController: 7 weak control iVars (`_settingsEnginePopup_v551_`, `_settingsSyncSourcePopup_v551_`, `_settingsMidiMappingPopup_v551_`, `_settingsThemePopup_v551_`, `_settingsLangPopup_v551_`, `_settingsRouterToggle_v551_`, `_settingsDashToggle_v551_`) captured via `ARPSID_SETTINGS_BUILD_SECTION` macro `outIvar` parameter + ADVANCED section toggle refs. `_pushSettingsToAdapter_v551_` called at end of all 7 action handlers. `_applySettingsModelToSettingsPanel_v551_` updates all 7 controls from model. On `_connectBridgeInternal:`, pulls restored model from adapter and applies to UI. `ArpSIDDebugAdapterLike` `@optional getSettingsModel:` + `setSettingsModel:`. `settings_persistence_v551_tests.cpp`: 6 sections (layout/schema static_asserts, defaults well-formed, serialize→deserialize round-trip bit-identical, sanitize clamping, schema mismatch reset, byte layout spot-check). 86/86 ctest passing. |
| **v552** SETTINGS theme application | GUI | `theme_palette_v552.h` — pure C++ header: `ThemeColorRGBA_v552` (RGBA float atom, trivially copyable), `ThemeColorSet_v552` (9 color roles: bg/title/label/value/border/accent/inactive/ledOn/ledOff, trivially copyable), `constexpr themePalette_v552(Theme)` returns compile-time color set for all 4 themes (Dark/Light/C64Classic/HighContrast) with full alpha invariant and title≠label/accent≠inactive static_asserts. ViewController: `_panelContentRect_v552_` iVar stored at panel-build time. 5 theme-aware instance methods (`_themeColor_v552_:`, `_themeTitle_v552_`, `_themeLabel_v552_`, `_themeValue_v552_`, `_themeAccent_v552_`, `_themeBg_v552_`) read current theme and return fresh `NSColor` (not statically cached). `_settingsPanel_v544_:` + `_c64StatePanel_v544_:` builders changed to use `[self _theme*_v552_]` for section headers, labels, value readouts, and background tint. `_applyTheme_v552_` rebuilds `_pSettingsV544` and `_pC64StateV544` in-place from stored rect + re-applies `_showTab:`. `_applyThemeButtonPressed_v552_:` button action (↺ Reapply Theme button added to SETTINGS panel). `_settingsThemeChanged_v544_:` calls `_applyTheme_v552_` immediately after model update. `settings_theme_v552_tests.cpp`: 9 sections (layout static_asserts, compile-time alpha invariants, all-themes opaque, 4× palette pinning, roles-distinct, themes-mutually-distinct). 87/87 ctest passing. |
| **v553** SETTINGS language application | GUI | `language_strings_v553.h` — pure C++ header (no Cocoa, no .lproj files): `UIStringKey_v553` enum (28 keys, kKeyCount pinned by static_assert) covering all localizable strings in SETTINGS + C64STATE panels (6 section headers, 5 sub-labels, 3 engine items, 4 sync items, 4 MIDI items, 4 theme items, 1 button, 1 panel title). `constexpr localizedString_v553(UIStringKey_v553, Language)` dispatches to 5 per-language helper functions (en/no/de/fr/jp) — all strings UTF-8 hex-escaped for ASCII-safe source; hex escape greedy-parse bugs fixed with `""` string concatenation where needed (`\xa7" "8`, `\xa9" "appliquer`, `\xbc" "fstabilisierung`). Norwegian (Bokmål), German, French, Japanese translations cover all 28 keys. Japanese encoded as UTF-8 hex escapes. Compile-time invariants: all English keys non-empty, Norwegian byte[4] ≠ English byte[4] for kSectionAudioEngine. ViewController: `-(NSString*)_localStr_v553_:(UIStringKey_v553)key` helper (bridge to language table). All 5 macro invocations in `_settingsPanel_v544_:` updated to use `[self _localStr_v553_:K::k...]`. ADVANCED header + "Reapply Theme" button + C64STATE panel title all localized. `_settingsLanguageChanged_v544_:` calls `_applyTheme_v552_` to rebuild panels with new language strings. `settings_language_v553_tests.cpp`: 9 sections (kKeyCount pin, English key non-empty, all 140 key×lang non-empty, ◈-prefix check, 3× language distinctness, JP non-empty + ◈-prefix, all non-English languages translate kSectionAudioEngine). 88/88 ctest passing. |
| **v554** MIX FX processor contracts | TIER 1.2 | `include/arpsid/audio/mix_fx_processors.h` (new `audio/` directory) — RT-safe FX processor chain for MIX tab. `BiquadCoeffs` (20 bytes, identity default), `BiquadState` (16 bytes, Direct Form II Transposed), coefficient helpers `computeLowShelfCoeffs`/`computePeakingBellCoeffs`/`computeHighShelfCoeffs` (Audio EQ Cookbook, shelf slope S=1). Bug fix in v556 pass: `computeLowShelfCoeffs` a0 denominator used wrong sign `- (A-1)*cosw` (high-shelf formula) instead of correct `+ (A-1)*cosw` (low-shelf formula), causing an unstable pole at |z|=1.036. Fixed. Five processor types: `Eq3BandProcessor` (low shelf + mid bell + high shelf, 3 biquads per channel L+R, ≤256 bytes), `TransientProcessor` (fast/slow dual-envelope peak follower, separate attack/sustain gains, ≤64 bytes), `CompressorProcessor` (feed-forward VCA, peak detector, dB-domain ballistics, ≤64 bytes), `SaturatorProcessor` (ParameterSmoother<float> drive, 3 characters tape/tube/transistor, ≤48 bytes), `BitcrusherProcessor` (4–16 bit quantisation + 1–16x ZOH downsampling, ≤48 bytes). `MixFxProcessor` tagged struct (no union, all 5 processors present, switch dispatch, bypass flag, ≤512 bytes) — trivially copyable, no vtable. `applyMixFxChain()` serial-applies kMixFxSlotsPerChannel=5 processors. All types trivially copyable (pinned). `mix_fx_processors_v554_tests.cpp`: 12 sections (layout pins, BiquadState identity, flat EQ, 12 dB shelf boost, compressor below/above threshold, bitcrusher transparent/crush, transient unity, saturator bounded, bypass flag, serial chain). |
| **v555** KIT tab data model | TIER 1.3 | `include/arpsid/gui/kit_panel_model.h` — 9 canonical drum classes (Kick/Snare/ClosedHat/OpenHat/Clap/Rim/Tom/Cowbell/Crash), 3 engine targets (DrSID/SID808/Digi), `KitPanelModel` POD (924 bytes, trivially copyable, well-formed validated at compile time). `kKitDrumClassMidiNote[9]` (all in GM drum range 35..81), `kKitDrumClassLabel[9]` (all ≤8 chars). Factory slot ranges: DrSID 80–119 (40 slots), SID-808 120–149 (30 slots), Digi 150–179 (30 slots). `kitSlotCount`/`kitAbsoluteSlot` constexpr helpers. `KitUserSlotMeta` (20 bytes): name[16] + hasUserName flag. `kitSetUserSlotName`/`kitDisplayName` produce factory-style "DrSID #00" / "S808 #05" / "Digi #29" when unnamed. Default: drum class N → slot N for all engine targets. `kit_panel_model_v555_tests.cpp`: 10 sections. |
| **v556** KIT EDIT tab NSView builder + wire-up | GUI | `ArpSIDTabKitV555=17` added to NS_ENUM; all 4 flavor visible-tab arrays updated (Hybrid 18, Instrument 15, DrumMachine 16, SID808 16 entries); labels `@"◈ KIT EDIT"` + flavor-specific tooltips wired in all 4 branches; `_pKitV555` + `_kitModel_v555_` iVars; `_activeChromeHostPanel` + `_showTab:` cases added. `_kitPanel_v555_:` Cocoa builder: header row (title + DrSID/SID-808/Digi engine target buttons + Step/Voice/Assign editor mode buttons), drum class sidebar (9 NSButton toggle, tag kArpSIDKitDrumTagBase+dc), user slot scrollable browser (40/30/30 slots per target, factory display name, tag kArpSIDKitSlotTagBase+si), center scaffold NSView (replaced in v557 with live 32-step grid). Action handlers: `_kitDrumClassChanged_v556_:`, `_kitEngineTargetChanged_v556_:`, `_kitEditorModeChanged_v556_:`, `_kitUserSlotSelected_v556_:` — all update model + deselect sibling buttons via tag lookup. `kit_tab_wire_v556_tests.cpp`: 7 sections (layout pin, default model, slot counts, absolute addressing, assignment grid, display names, well-formedness guards for all tab action mutations). 91/91 ctest passing. |
| **v558** KIT voice config model + voice editor panel | GUI | `include/arpsid/gui/kit_voice_config.h` — `KitVoiceConfig` POD (8 bytes, trivially copyable): `waveform` (SID CR bits [7:4], lower nibble must be 0), `attackDecay` ((A<<4)|D), `sustainRelease` ((S<<4)|R), `pulseWidthLo/Hi` (12-bit PW, hi upper nibble must be 0), `flags` (bit0=ringMod, bit1=hardSync, bit2=filterRoute, bits[7:3] must be 0), `pad_[2]`. `KitVoiceConfigGrid` (80 bytes): 4-byte schema + 4-byte pad + 9×8=72-byte `voiceConfigs[kKitDrumClassCount]`. Waveform constants `kKitVoiceWaveTri/Saw/Pul/Noi/Mask` (0x10/0x20/0x40/0x80/0xF0). Accessors: `kitVoiceAttack/Decay/Sustain/Release` (nibble extract), `kitVoicePulseWidth` (lo|hi<<8), `kitVoiceIsWave`, `kitVoiceRingMod/HardSync/FilterRoute`. Mutators: `kitVoiceSetWaveform/Bit/ClearBit/ToggleBit`, `kitVoiceSet{Attack,Decay,Sustain,Release}` (clamp 0–15), `kitVoiceSetPulseWidth` (clamp 0–4095), `kitVoiceSet{RingMod,HardSync,FilterRoute}`. `kitVoiceConfigIsWellFormed`/`kitVoiceConfigGridIsWellFormed`. Default: noise (0x80), A=0/D=8/S=0/R=4, PW=2048, all flags off. ViewController: `#include "arpsid/gui/kit_voice_config.h"`; `_kitVoiceConfigGrid_v558_` iVar + `makeDefaultKitVoiceConfigGrid()` init; `_pKitStepEdView_v558_` / `_pKitVoiceEdView_v558_` iVars (NSView.tag is readonly in AppKit — iVars used for show/hide); voice editor tag constants `kArpSIDKitVoiceWaveTagBase=0xA000`, `kArpSIDKitVoiceADSRTagBase=0xA010`, `kArpSIDKitVoiceADSRLblTagBase=0xA014`, `kArpSIDKitVoicePWTag=0xA020`, `kArpSIDKitVoicePWLabelTag=0xA021`, `kArpSIDKitVoiceFlagTagBase=0xA030`; step grid edView sets `_pKitStepEdView_v558_`; voice editor NSView (waveform selector TRI/SAW/PUL/NOI buttons + ADSR stepper+label groups + PW NSSlider + RING/SYNC/FILT flag buttons, all labelled + tagged) sets `_pKitVoiceEdView_v558_`, initially hidden; `_kitEditorModeChanged_v556_:` shows/hides iVar views + calls step/voice refresh; `_kitDrumClassChanged_v556_:` also calls `_kitRefreshVoiceEditor_v558_` when mode=1; action handlers: `_kitVoiceWaveChanged_v558_:`, `_kitVoiceADSRChanged_v558_:`, `_kitVoicePWChanged_v558_:`, `_kitVoiceFlagChanged_v558_:`; `_kitRefreshVoiceEditor_v558_` refreshes all 13 controls from model. `kit_voice_config_v558_tests.cpp`: 7 sections (layout pin, default config+grid, well-formedness guards, ADSR nibble round-trip+clamp, PW byte-split+clamp, waveform bit operations, flag bit independence, full 9-class coverage). 93/93 ctest passing. |
| **v568** LFO rate-curve fix | correctness | `include/arpsid/core/sid_runtime_parameter_services.h` LFO rate dispatch (slot==0 branch inside `runtimeApplyProjectedHostCtrl`): replaced LINEAR Hz mapping (`rateHz = 0.1 + value × 19.9`) with EXPONENTIAL mapping (`rateHz = clamp(0.1 × 200^value, 0.01, 20)`). The old linear formula placed the knob midpoint at ≈10.05 Hz — above the entire vibrato/tremolo register (4–8 Hz) — making the musically useful sub-5 Hz range crammed into the bottom quarter of the knob. The correct exponential formula places the geometric midpoint at √(0.1 × 20) = √2 ≈ 1.414 Hz (slow vibrato). Endpoints unchanged: value=0 → 0.1 Hz, value=1 → 20 Hz. LFO::setRateTempo (host-sync BPM path) unchanged. `lfo_rate_exp_v568_tests.cpp`: 6 sections (endpoints 0.1/20 Hz shared with old formula; geometric midpoint √2 ≈ 1.414 Hz, < 5 Hz, old midpoint > 7× faster; monotone increase across 20 sample points; clamp guards for value<0 and value>1; setRateTempo BPM path directly verified via LFO::process() + getPhase(); phase-increment consistency at 44100 and 48000 Hz for all three key rates). 103/103 ctest passing. |
| **v567** Arpeggiator rate-curve fix | correctness | `include/arpsid/engines/arpeggiator.h` `Arpeggiator::setRate(float value)`: replaced LINEAR Hz mapping (`rateHz = 0.1 + value × 49.9`) with EXPONENTIAL mapping (`rateHz = clamp(0.1 × 500^value, 0.01, 50)`). The old linear formula placed the knob midpoint at 25.05 Hz — 11× above any musical arpeggio tempo — making the useful range (1–8 Hz) crammed into the bottom 15% of the knob. The correct exponential formula places the geometric midpoint at √(0.1 × 50) = √5 ≈ 2.236 Hz (quarter-note at ~134 BPM). Endpoints unchanged: value=0 → 0.1 Hz, value=1 → 50 Hz. `setRateTempo(bpm, division)` (host-sync path) unchanged. `arpeggiator_rate_exp_v567_tests.cpp`: 6 sections (endpoints 0.1/50 Hz, geometric midpoint √5 ≈ 2.236 Hz, monotone increase across 20 sample points, clamp guards for value<0 and value>1, setRateTempo BPM path unchanged, stepSamplesForStep consistency at 44100 and 48000 Hz). 102/102 ctest passing. |
| **v566** DIGI tab status promotion | GUI contract | `include/arpsid/gui/tab_architecture.h`: promoted DIGI from `Scaffold` → `Implemented` in `kArpSIDTabs[2]` (comment `// v563-v565`). Updated per-tab status static_assert for DIGI: `TabImplementationStatus::Scaffold` → `Implemented` with message `"DIGI is implemented (v563-v565)"`. Updated count static_asserts: `implementedTabCount() == 7` → `8` (message updated to list DIGI); `scaffoldTabCount() == 2` → `1` (C64STATE is now the sole scaffold tab). Fixed 3 stale test files: `gui_tab_architecture_v543_tests.cpp` (count require() calls 7/2 → 8/1, `isTabScaffold(DIGI)` → `isTabImplemented(DIGI)`); `v544_tab_wire_completeness_v546_tests.cpp` (count require() calls 7/2 → 8/1); `tab_architecture_promotion_v562_tests.cpp` (static_assert count pins 7/2 → 8/1, section III header + DIGI static_assert `isTabScaffold` → `isTabImplemented`, runtime count asserts 7/2 → 8/1). `tab_architecture_promotion_v566_tests.cpp`: 6 sections (compile-time count pins 8/1, DIGI promoted to Implemented with correct v563-v565 attribution, C64STATE remains Scaffold, all 8 Implemented tabs individually static_asserted, runtime inventory consistency — array/tabSpec accessor parity + DIGI index 2 Implemented + C64STATE index 7 Scaffold, HUD label ≤6 chars + non-null display/description for all 9 tabs). 101/101 ctest passing. |
| **v565** DIGI AU state persistence | GUI+AU | `ArpSIDDSPKernelAdapter.h`: added `(DigiStatePersistence)` category declaration (`getDigiModel:` / `setDigiModel:`); both declared as `@optional` in `ArpSIDDebugAdapterLike` protocol in ViewController. `ArpSIDDSPKernelAdapter.mm`: `_digiModel_v565_` iVar (`DigiPanelModel`, initialized to `makeDefaultDigiPanelModel()` in `-init`); `(DigiStatePersistence)` category — `getDigiModel:` (plain copy out), `setDigiModel:` (copy in + `sanitizeDigiPanelModel`). `ArpSIDAudioUnit.mm`: `_restoreDigiStateFromStateDictionary:` helper (validates NSData length == `sizeof(DigiPanelModel)` == 328, memcpy, calls `setDigiModel:` on adapter); `fullState` serializes 328-byte model to `@"ArpSIDDigi_v565"` NSData key (via `getDigiModel:`); `setFullState:` + `setFullStateForDocument:` both call `_restoreDigiStateFromStateDictionary:` (before MIX restore). `ArpSIDViewController.mm`: `_pushDigiStateToAdapter_v565_` (copies `_digiModel_v563_` to adapter; called at end of all 9 DIGI action handlers: `_digiSlotSelected_v563_:`, `_digiStepToggled_v563_:`, `_digiSourceChanged_v563_:`, `_digiFactorySlotChanged_v563_:`, `_digiTuneChanged_v563_:`, `_digiStartChanged_v563_:`, `_digiLengthChanged_v563_:`, `_digiVolumeChanged_v563_:`, `_digiFlagChanged_v563_:`); `_pullDigiStateFromAdapter_v565_` (gets model from adapter, copies to `_digiModel_v563_`, refreshes all DIGI controls via tags — slot button selection loop + `_digiRefreshSlotControls_v563_` + `_digiRefreshStepGrid_v563_`; unlike MIX, no panel rebuild required since all DIGI controls are tagged); pull called in `_connectBridgeInternal:` dispatch block after MIX pull. `digi_state_persistence_v565_tests.cpp`: 6 sections + 2 compile-time static_asserts (blob size pin 328 bytes, `sizeof(DigiPanelModel)==328`; default model memcpy round-trip via 328-byte blob with sanitize; mutated slot 3 FactorySlot round-trip with tuneShift −4 / start / length / volume / loop; step grid round-trip every-other/every-third steps in slots 0+7; sanitize idempotent on valid blob; sanitize resets corrupt schema to defaults; out-of-range tuneShiftBias triggers all-or-nothing reset). 100/100 ctest passing. |
| **v564** DIGI tab NSView builder + wire-up | GUI | `ArpSIDViewController.mm`: added `ArpSIDTabDigiV563=18` to `ArpSIDTab` NS_ENUM. All 4 visible-tab arrays updated: Hybrid 17→18, Instrument 15→16, DrumMachine 16→17, Sid808 16→17 — each now appends `ArpSIDTabDigiV563` at end so existing host automation segment indices remain stable. Added `NSView*_pDigiV563` iVar + `ArpSID::GUI::DigiPanelModel _digiModel_v563_` iVar (initialized to `makeDefaultDigiPanelModel()` in `-init`). Tab label `@"◈ DIGI"` wired in all 4 flavor label switch branches + fallback. Tooltip `DIGI tab (v564). $D418 volume-DAC sample player...` wired in all 4 branches. `_activeChromeHostPanel` switch: `case ArpSIDTabDigiV563`. `_showTab:` visibility cascade: `_pDigiV563.hidden`. Panel built as `_pDigiV563 = [self _digiPanel_v563_:cr]` and added to root `addSubview` array. Tag constants: `kArpSIDDigiSlotTagBase=0xC000` (8 slots), `kArpSIDDigiStepTagBase=0xC100` (32 steps), 13 control tags in 0xC200..0xC251 range — no collision with KIT (≤0xBFFF) or MIX (≤0x5FFF). `_digiPanel_v563_:` builder: header "◈ DIGI SAMPLE PLAYER", left sidebar 8 `NSButtonTypePushOnPushOff` slot buttons (SLOT 1..8), right area: row 1 SOURCE NSPopUpButton (None/Factory/UserImport) + SLOT NSStepper+label + TUNE NSStepper+label, row 2 START NSSlider+label, row 3 LEN NSSlider+label (0→"FULL"), row 4 VOL NSSlider+label + LOOP/REV NSCheckbox buttons; bottom: 32-step grid in 2-row×16-col layout inside a container NSView with border, beat-boundary cols use bold font. Action handlers: `_digiSlotSelected_v563_:` (model.activeSlot + sibling deselect + refresh), `_digiStepToggled_v563_:` (toggle + state re-sync), `_digiSourceChanged_v563_:`, `_digiFactorySlotChanged_v563_:`, `_digiTuneChanged_v563_:` (±12 clamp), `_digiStartChanged_v563_:`, `_digiLengthChanged_v563_:`, `_digiVolumeChanged_v563_:`, `_digiFlagChanged_v563_:`. Refresh helpers: `_digiRefreshSlotControls_v563_` (all 13 tagged controls), `_digiRefreshStepGrid_v563_` (all 32 step buttons). `#include "arpsid/gui/digi_panel_model.h"` added. `digi_panel_tab_v564_tests.cpp`: 6 sections (tab enum pin, visible-tab array counts, model layout unchanged, tag range non-collision + 53 distinct DIGI tags, loop bounds 8×32, default model matches builder initial state). 99/99 ctest passing. |
| **v563** DIGI tab data model | GUI | `include/arpsid/gui/digi_panel_model.h` — complete DIGI tab POD. `DigiSourceType` enum (None=0 / FactorySlot=1 / UserImport=2). `DigiSampleSlot` (8 bytes, trivially copyable): `sourceType`, `factorySlotIndex` (0..29), `tuneShiftBias` (biased ±12 st; 128=0), `startOffset` (0..255), `lengthScale` (0=full), `volume` (0..255, default 200), `flags` (bit0=loop, bit1=reverse), `pad_[1]`. `DigiPanelModel` (328 bytes, trivially copyable): 4-byte schema + 1-byte activeSlot + 3-byte pad (header 8B @offset 0) + `slots[8]` (64B @offset 8) + `steps[8][32]` (256B @offset 72). Constants: `kDigiPanelSchemaVersion=1`, `kDigiActiveSlotCount=8`, `kDigiStepCount=32`, `kDigiStepMaxVelocity=127`, `kDigiStepDefaultVelocity=100`, `kDigiTuneBias=128`, `kDigiTuneBiasMin=116`, `kDigiTuneBiasMax=140`, `kDigiFlagLoop=0x01`, `kDigiFlagReverse=0x02`, `kDigiFlagMask=0x03`. `digiSampleSlotIsWellFormed` (sourceType ≤ UserImport, FactorySlot → factorySlotIndex < kKitDigiSlotCount, tuneShiftBias ∈ [116..140], reserved flags == 0). `digiPanelIsWellFormed` (schema + activeSlot + all slots + all step velocities ≤ 127). `makeDefaultDigiSampleSlot` / `makeDefaultDigiPanelModel` (compile-time default, static_assert well-formed). Accessors: `digiTuneShift`, `digiLoopEnabled`, `digiReverseEnabled`, `digiStepIsActive`, `digiStepVelocity` (OOB-safe). Mutators: `digiSetTuneShift` (±12 clamp), `digiSetFactorySlot` (sets sourceType, clamps index), `digiSetLoop`, `digiSetReverse`, `digiStepSetActive` (velocity clamp + 0→default), `digiStepSetInactive`, `digiStepToggle`, `digiStepClearSlot`. `sanitizeDigiPanelModel` (all-or-nothing reset). `digi_panel_model_v563_tests.cpp`: 9 sections (layout pins, default model spot-checks, slot well-formedness guards, panel well-formedness guards, accessor round-trips, mutator contracts incl. full 8×32 step coverage, sanitize no-op, sanitize reset, memcpy round-trip). 98/98 ctest passing. |
| **v562** Tab architecture status promotion | GUI contract | `include/arpsid/gui/tab_architecture.h`: promoted 4 tabs from `Scaffold` → `Implemented` in `kArpSIDTabs` array: KIT (v555–v560), MIX (v547–v561), SIDCORE (v545–v550), SETTINGS (v544–v553). C64STATE remains Scaffold (diagnostic counters only; full CPU/CIA/memory-map inspector not yet built). DIGI remains Scaffold (no sample-player implementation). Updated all 9 per-tab status `static_assert`s — previously-scaffold tabs now assert `Implemented`. Updated count static_asserts: `implementedTabCount() == 3` → `7`; `scaffoldTabCount() == 6` → `2`. Fixed two stale tests (`gui_tab_architecture_v543_tests.cpp` and `v544_tab_wire_completeness_v546_tests.cpp`) that hardcoded the v543 counts. `tab_architecture_promotion_v562_tests.cpp`: 6 sections (compile-time count pins 7/2, promoted tabs Implemented static_asserts, DIGI/C64STATE Scaffold static_asserts, previously-implemented tabs still Implemented, runtime inventory consistency — ids match enum order + tabSpec accessor parity, HUD label ≤6 chars + non-null display/description for all 9 tabs). 97/97 ctest passing. |
| **v561** MIX AU state persistence | GUI+AU | `include/arpsid/gui/mix_panel_model.h`: added `constexpr void sanitizeMixModel(MixPanelModel& m)` — resets whole model to `makeDefaultMixModel()` if `!mixModelIsWellFormed(m)` (all-or-nothing, since MIX well-formedness checks cover the structure atomically). `ArpSIDDSPKernelAdapter.h`: added `(MixStatePersistence)` category declaration (`getMixModel:` / `setMixModel:`); `getMixModel:` + `setMixModel:` declared as `@optional` in `ArpSIDDebugAdapterLike` protocol in ViewController. `ArpSIDDSPKernelAdapter.mm`: `_mixModel_v561_` iVar (`MixPanelModel`, initialized to `makeDefaultMixModel()` in `-init`); `(MixStatePersistence)` category — `getMixModel:` (plain copy out), `setMixModel:` (copy in + `sanitizeMixModel`). `ArpSIDAudioUnit.mm`: `_restoreMixStateFromStateDictionary:` helper (validates NSData length == `sizeof(MixPanelModel)`=1264, memcpy, calls `setMixModel:` on adapter); `fullState` serializes 1264-byte model to `@"ArpSIDMix_v561"` NSData key (via `getMixModel:`); `setFullState:` + `setFullStateForDocument:` both call `_restoreMixStateFromStateDictionary:` (before KIT restore). `ArpSIDViewController.mm`: `_pushMixStateToAdapter_v561_` (copies `_mixModel_v547_` to adapter; called at end of all 5 MIX action handlers: `_mixVolumeChanged_v548_:`, `_mixPanChanged_v548_:`, `_mixSoloToggled_v548_:`, `_mixMuteToggled_v548_:`, `_mixFxSlotChanged_v548_:`); `_pullMixStateFromAdapter_v561_` (gets model from adapter, copies to `_mixModel_v547_`, rebuilds `_pMixV547` by calling `_mixPanel_v547_:` with preserved frame/hidden state — handles untagged master+send bus controls correctly); pull called in `_connectBridgeInternal:` dispatch block after KIT pull. `mix_state_persistence_v561_tests.cpp`: 6 sections (static layout pin — `sizeof(MixPanelModel)==1264`, all sub-struct sizes, schema version, trivially copyable, compile-time default well-formed; default model field spot-checks; sanitize no-op on well-formed; sanitize schema mismatch → full reset; sanitize bad per-channel field (enabled>1 / FX type OOR / selectedChannel OOR); memcpy round-trip with full field preservation). 96/96 ctest passing. |
| **v560** KIT AU state persistence | GUI+AU | `include/arpsid/gui/kit_state_blob.h` — `KitStateBlob` POD (1388 bytes, trivially copyable): 4-byte schema (`kKitStateBlobSchemaVersion=1`) + 4-byte pad + `KitPanelModel` (924B @offset 8) + `KitStepGrid` (296B @offset 932) + `KitVoiceConfigGrid` (80B @offset 1228) + `KitAssignConfigGrid` (80B @offset 1308). `kitStateBlobPack`/`kitStateBlobUnpack` (constexpr copy in/out all four sub-models). `kitStateBlobIsWellFormed` (checks schema + all four sub-model well-formedness). `kitStateBlobSanitize` (field-by-field: each bad sub-model independently reset to default; good sub-models survive). `makeDefaultKitStateBlob` (compile-time default, static_assert well-formed). `ArpSIDDSPKernelAdapter.h` / `.mm`: `_kitStateBlob_v560_` iVar (initialized to `makeDefaultKitStateBlob()` in `-init`); `(KitStatePersistence)` category — `getKitStateBlob:` (plain copy out), `setKitStateBlob:` (copy in + `kitStateBlobSanitize`). `ArpSIDAudioUnit.mm`: `_restoreKitStateFromStateDictionary:` (validates NSData length == 1388, memcpy into blob, calls `setKitStateBlob:` on adapter); `fullState` serializes blob to `@"ArpSIDKit_v560"` NSData key; `setFullState:` + `setFullStateForDocument:` both call `_restoreKitStateFromStateDictionary:`. `ArpSIDViewController.mm`: `#include "arpsid/gui/kit_state_blob.h"`; `getKitStateBlob:` + `setKitStateBlob:` declared as `@optional` in `ArpSIDDebugAdapterLike` protocol; `_pushKitStateToAdapter_v560_` (packs four iVar models, calls adapter `setKitStateBlob:`; called at end of all 14 KIT action handlers); `_pullKitStateFromAdapter_v560_` (gets blob, unpacks to four iVars, refreshes all KIT UI — drum class/engine/mode buttons, editor panel visibility, step grid + voice/assign editors); called in `_connectBridgeInternal:` dispatch block after settings pull. `kit_state_blob_v560_tests.cpp`: 6 sections (layout pin, default blob sub-models match canonical defaults, pack→unpack round-trip bit-identical, isWellFormed guards, sanitize field-by-field independence, memcpy round-trip via raw byte array). 95/95 ctest passing. |
| **v559** KIT assign config model + assign editor panel | GUI | `include/arpsid/gui/kit_assign_config.h` — `KitAssignConfig` POD (8 bytes, trivially copyable): `digiSlotIndex` (0-based Digi factory slot, 0..kKitDigiSlotCount-1=0..29), `tuneShiftBias` (semitone offset biased: 128=0st, 116=−12st, 140=+12st), `startOffset` (0..255), `lengthScale` (0=full, 1..255=scaled), `flags` (bit0=loopEnabled, bit1=reversePlayback, bits[7:2]=0), `engineTargetOverride` + `pad_[2]`. `KitAssignConfigGrid` (80 bytes): 4-byte schema + 4-byte pad + 9×8=72-byte `assignConfigs[kKitDrumClassCount]`. Constants `kKitAssignSchemaVersion=2`, `kKitAssignTuneBias=128`, `kKitAssignTuneMin=−12`, `kKitAssignTuneMax=+12`, `kKitAssignFlagLoop=0x01`, `kKitAssignFlagReverse=0x02`, `kKitAssignFlagMask=0x03`. Accessors: `kitAssignSlot`, `kitAssignTuneShift` (returns `(int8_t)(tuneShiftBias-128)`), `kitAssignStartOffset`, `kitAssignLengthScale`, `kitAssignLoopEnabled`, `kitAssignReverseEnabled`. Mutators: `kitAssignSetSlot` (clamp 0..kKitDigiSlotCount-1), `kitAssignSetTuneShift` (clamp ±12, stores biased), `kitAssignSetStartOffset`, `kitAssignSetLengthScale`, `kitAssignSetLoop`, `kitAssignSetReverse`. `kitAssignConfigIsWellFormed` (slot<kKitDigiSlotCount, tuneShiftBias∈[116,140], engineTargetOverride∈{0,1,2,255}, flags&~0x03==0). Default: drum class N → slot N, bias=128, start=0, length=0, flags=0. ViewController: `#include "arpsid/gui/kit_assign_config.h"`; `_kitAssignConfigGrid_v559_` iVar + `makeDefaultKitAssignConfigGrid()` in `-init`; `_pKitAssignEdView_v559_` iVar; assign editor tag constants `kArpSIDKitAssignSlotTag=0xB000`, `kArpSIDKitAssignSlotLblTag=0xB001`, `kArpSIDKitAssignTuneTag=0xB010`, `kArpSIDKitAssignTuneLblTag=0xB011`, `kArpSIDKitAssignStartTag=0xB020`, `kArpSIDKitAssignStartLblTag=0xB021`, `kArpSIDKitAssignLenTag=0xB030`, `kArpSIDKitAssignLenLblTag=0xB031`, `kArpSIDKitAssignFlagTagBase=0xB040`; assign editor NSView in `_kitPanel_v555_:` (SLOT NSStepper+label, TUNE NSStepper+label with %+d format, START NSSlider+label, LEN NSSlider+label (0→"FULL"), LOOP/REV flag buttons), sets `_pKitAssignEdView_v559_`, initially hidden; `_kitEditorModeChanged_v556_:` shows/hides assign view + calls `_kitRefreshAssignEditor_v559_` when mode=2; `_kitDrumClassChanged_v556_:` calls refresh when mode=2; action handlers: `_kitAssignSlotChanged_v559_:`, `_kitAssignTuneChanged_v559_:`, `_kitAssignStartChanged_v559_:`, `_kitAssignLengthChanged_v559_:`, `_kitAssignFlagChanged_v559_:`; `_kitRefreshAssignEditor_v559_` refreshes all 9 controls from model. `kit_assign_config_v559_tests.cpp`: 8 sections (layout pin, default config+grid, well-formedness guards, slot round-trip+clamp, tune shift bias+clamp, start/length full uint8 range, flag independence, full 9-class coverage). 94/94 ctest passing. |
| **v557** KIT step grid model + 32-button editor | GUI | `include/arpsid/gui/kit_step_grid.h` — `KitStepGrid` POD (296 bytes, trivially copyable): `uint32_t schemaVersion` + `uint8_t stepCount` + `uint8_t pad_[3]` + `uint8_t steps[kKitDrumClassCount][kKitStepCount]` (9×32=288 bytes). Each step: 0=inactive, 1–127=active+velocity. Constants `kKitStepSchemaVersion=1`, `kKitStepCount=32`, `kKitStepDefaultVelocity=100`, `kKitStepMaxVelocity=127`. Accessors `kitStepIsActive`, `kitStepVelocity`. Mutators `kitStepSetActive` (velocity clamp 1–127, 0 snapped to default), `kitStepSetInactive`, `kitStepToggle` (inactive→100/active→0), `kitStepClearDrumClass`, `kitStepClearAll`. `kitStepGridIsWellFormed` (schema + stepCount + all velocities ≤127). ViewController: `#include "arpsid/gui/kit_step_grid.h"`; `_kitStepGrid_v557_` iVar; `makeDefaultKitStepGrid()` in `-init`; `kArpSIDKitStepTagBase=0x8000`; `_kitStepToggled_v557_:` action (toggle + model-derived re-sync of button state); `_kitRefreshStepGrid_v557_` (reads active drum class, loops 32 tags, syncs all button states); `_kitDrumClassChanged_v556_:` calls `[self _kitRefreshStepGrid_v557_]` after sibling-deselection. Center editor: scaffold NSView replaced with 2-row × 16-col grid of 32 `NSButtonTypePushOnPushOff` step buttons (beat-boundary steps cols 0/4/8/12 use `monoBF(7.5)`, others `monoF(7.0)`; tag = base + stepIdx; initial state derived from `_kitStepGrid_v557_` at build time). `kit_step_grid_v557_tests.cpp`: 10 sections (static layout pin, default grid, well-formedness guard, read accessors OOB-safe, setActive velocity clamping, setInactive, toggle round-trip, clearDrumClass sibling safety, clearAll, full 9×32 coverage). 92/92 ctest passing. |

## Open audit items (out of scope for single-session skives)

| Audit | Why deferred |
|---|---|
| #46 mutex-mengde i AUv2 instance | Strukturell — krever klassesplitt |
| #47 mid-render epoch + kernel rollback | Multi-uke kjerne-refaktor |
| #53, #54 kernel pending-state drain | Multi-tusen LOC kjerne-arkitektur-refaktor |
| #57 transient param vs persistent | Mindre kritisk hygiene |
| #64 PSID retire-old-player race | Eksisterende implementasjon antas korrekt (audit-flagged som "complex lifetime path", ikke konkret bug) |
| #68 disabled CMake test gates | Deler av disse er nå reaktivert via audit-stabiliserings-tests |
| #70 AUv2 smoke test gating | v582 completed manual installed-component smoke + strict auval for this release candidate; future CI automation remains deferred |
| #73, #75–#78 arkitektoniske observasjoner | Måneds-refaktor |

## Total invariants pinned

- 130 ctest-tests, 100% pass rate through v603 full-feature/default-enable closure
- Installed AUv2 smoke test passed end-to-end (reset/preset retention, DrSID projection, 96k/192k reconfigure, ClassInfo stress)
- Strict AUv2 verifier/auval passed for all installed subtypes after cache clear/reinstall
- 20+ nye header-only kontrakter
- 15+ nye diagnostic counters i prod (alle observable via host-side debug paths)
- 0 known audio-regresjoner i eksisterende Logic-sessions; production infrastructure defaults ON where required for SID-808/telemetry, while patch-tone sends/effects remain patch-controlled

## Migration path for release-candidate

For Logic/Cubase/AUM hosts:
- v520+ headers and tests are additive — drop-in replacement, no host config change needed.
- Engine-split router is production default ON. Hosts still call `impl->drumEngineBridge.loadFactorySlot(slot)` when preset is in the SID-808 120-149 range; `impl->useDrumEngineRouter_.store(false)` remains the explicit compatibility disable path.
- New sessions expose SID-808 bridge routing and C64 STATE diagnostics by default. Saved projects that explicitly stored OFF keep that OFF state.
- Diagnostic counters surface via host debug-paths (`bridgeDivertedRenderCount`, `c64PsidVideoStandardFallbackCount`, etc.).

## PASS198 FINAL RELEASE CLOSURE

Built `arpsid_release_closure_suite` and ran the broad release regex: 71/71 passed. See PASS198_FINAL_RELEASE.md.

## PASS199 DIGI sampler product closure

Implemented export sample, pending-record KEEP/DISCARD/overwrite safety, record meters, honest System Default Input policy, truncation warnings, conversion weighting controls, sample rename/copy/swap, and DIGI kit import/export. See PASS199_DIGI_SAMPLER_PRODUCT_CLOSURE.md.

- PASS200: DIGI sampler completion closure: added active-slot audition, real drag/drop import view, MIDI learn root/channel workflow, clear-all user-bank safety, and regression guards.

## PASS201 DIGI Sampler 100% Product Closure

Closed final product-level DIGI sampler gaps: versioned/hash-checked kit import/export, explicit target-slot copy/paste clipboard, destructive active-sample trim UI/action, and initialization of all sampler workflow state. Targeted DIGI regression: 19/19 passed.

## PASS202_DIGI_UI_COMPLETION_CLOSURE
- Visible PASTE + destructive trim controls added to DIGI panel.
- ASM/S export added for active sample.
- COPY tooltip/action corrected to clipboard workflow.
- SWAP stale user-import reference clearing fixed.
- DIGI/GUI regression slice: 19/19 passed.

## PASS203 DIGI final selector/export lifetime closure

- Fixed KEEP recorded-take selector mismatch: old name: call replaced with sampleRate: using captured pending take rate.
- Fixed async export lifetime: save-panel completion now uses value-copied clip instead of a stack reference.
- Added regression guards and validated 19 DIGI/GUI/release tests.

- PASS205: closed DIGI record KEEP source frame-count bug. Bank-load now receives prepared take frame count while HUD/telemetry remains persisted clip.frameCount.

- PASS206: DIGI import fail-closed on empty prepared sample, status shows persisted 8 kHz/4-bit duration/TRUNC, and KIT OUT snapshots async state before save-panel completion.

## PASS207 — DIGI slot-dependent UI closure

- Continued from PASS206.
- Found remaining GUI-state issue: late-added sampler buttons were not centrally enabled/disabled by active-slot validity.
- Fixed `_digiRefreshSlotControls_v563_` to update EXPORT/RENAME/COPY/SWAP/CLEAR plus clipboard/trim controls from the active user sample state.
- Added regression guards to prevent stale enabled button state from returning.

## PASS208 DIGI final record-arm / metadata closure

- Added slot-local overwrite arm: `_digiRecordOverwriteArmedSlot_v208_`.
- REC overwrite warning is no longer reusable after switching slots.
- KEEP/DISCARD/failed STOP reset pending-take metadata.
- Pending-take meter uses captured pending take sample-rate and shows slot/drop state.
- Clear/clear-all invalidates stale clipboard/take/overwrite state.
- Validated with DIGI GUI full contract suite: 19/19 passing.

## PASS209 — DIGI pending-take coherency closure

Closed the final stale pending-take workflow hole: any slot replacement or deletion path now invalidates a pending take for that same slot, so KEEP cannot later overwrite newly imported/pasted/trimmed/cleared state with an older recording.

Validated with the DIGI GUI full contract suite: 19/19 tests passed.

## PASS210 — DIGI kit import state closure

- Closed stale state after `KIT IN`: pending take, overwrite arm and clipboard are cleared after successful full-kit import.
- Prevents old recorded takes or copied clips from being applied to a newly imported DIGI kit.
- Added `PASS210_DIGI_KIT_IMPORT_STATE_CLOSURE.md`.

## PASS211 DIGI async/variant state closure

- Added generation-guarded async import publish.
- Source/factory slot variants now invalidate stale pending takes and stale imports.
- Clear-all resets pending take rate/slot metadata.
- Validation: 19/19 targeted DIGI/GUI/release tests passed.

## PASS212 DIGI mutation generation closure

Closed final async variant holes: record KEEP/commit, destructive trim and clear-slot now bump the DIGI sample workflow generation so stale async imports cannot overwrite newly committed/trimmed/cleared sample state.


## PASS213 DIGI async import failure HUD closure
- Added generation-gated async import failure HUD helper.
- Routed import open/empty/buffer/read/format/silence/load failure paths through same generation guard as publish path.
- Built arpsid_digi_gui_full_contract_suite and ran 19 relevant DIGI tests: 100% passed.


## PASS214 DIGI stale import success HUD closure

Closed final stale async success-HUD variant: stale async import success now returns silently and cannot overwrite newer workflow status. Added source regression guards.


# PASS215 DIGI user-import empty-slot closure

- Selecting User Import with no existing user clip no longer leaves sourceType=UserImport with handle=0 if the import panel is pending/cancelled.
- The slot stays NoSource until successful import publishes a real handle.

- PASS216: Fixed final DIGI audit findings: false TRUNC detection, centralized user sample identity, honest empty-slot audition, clean source-only release packaging.


## PASS217 - DIGI record permission slot closure

- Fixed pending microphone permission callback to preserve the slot where REC was requested.
- Added regression guards for requested-slot metadata and restore-before-start behavior.


## PASS218_DIGI_PAD_AUDITION_CLOSURE

Closed final GUI pad audition variant: empty slots and missing adapter can no longer be displayed as queued successful audio.

## PASS219 DIGI visible audition enable closure

- Active-slot PLAY button now uses `_digiSlotHasPlayableSource_v218_` for visible enabled state.
- GUI audition pads now use `_digiSlotHasPlayableSource_v218_` for per-slot enabled state.
- Runtime click guards remain in place, so the UI and handler contract now agree.
- Validation: `arpsid_digi_gui_full_contract_suite` + release-no-junk, 19/19 passing.


## PASS220 — build/install closure

- Restored root `build.sh` required by `ReleaseNoJunkPlaceholderV703Tests`.
- Root build helper enables `ARPSID_BUILD_AUV2=ON` on macOS so `ArpSID.component` is actually built.
- Added `--install-user` mode to build, validate, install, refresh, and verify AUv2.
- Release remains source-only: no build directories, CTest metadata, cache files, or objects are shipped.

## PASS221 BUILD INSTALL VALIDATE FINAL CLOSURE

- Renamed release root and VERSION to PASS221.
- Hardened `build.sh` into a complete build/test/AUv2/install/cache-clear helper.
- `--install-user` now clears old AU/Logic caches, builds AUv2, installs, refreshes and attempts auval discovery.
- Preserved source-only packaging; no stale build/CTest/object artifacts included.

## PASS224 — AUv2 build script and DIGI compile closure

Closed the remaining PASS223 release wrapper gap. Root `build.sh` is restored and now supports test-only, AUv2 build, AUv2 install, AU/Logic cache clear, release-check, and package-release paths. The script builds `arpsid_auv2` before looking for `ArpSID.component` and delegates installation to the canonical macOS installer when available.

## PASS231 — DIGI REC Pure SID GUI Sync Closure

Closed GUI/HUD follow-through for the PASS230 internal Pure 1:1 SID REC source: persisted REC source selection, source popup and sample workflow gating while recording, live Pure SID capture peak/RMS in kernel/adapter status, and meter text that follows the selected source.


## PASS241 — DIGI REC/MON pull-graph closure

Added muted AVAudioEngine pull graph for CoreAudio REC/MON so input taps are actually driven instead of showing permanent zero with no callbacks.

## PASS242 — DIGI REC/MON source completion

- Added direct CoreAudio input-device enumeration to the REC source popup, so
  BlackHole/Loopback/Aggregate inputs can be selected explicitly even when they
  are not visible through AVCaptureDevice discovery.
- Kept CoreAudio Output Monitor fail-closed to avoid fake zero-meter routing.
- Added MON microphone-permission request/retry flow instead of only showing a
  static permission-needed HUD.
- Pure SID monitor uses configured bounded capture capacity.
- Restored root build.sh release helper.
