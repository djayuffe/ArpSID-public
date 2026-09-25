# ArpSID pass380 patching log

This patchset follows the recommended P0-first order from the deep audit: state restore correctness, closure unblock, realtime hardening, PSID continuous routing, strict/compatible truth, and SID register authority.

## 1. State restore correctness

### Files changed

- `source/au3/ArpSIDDSPKernel.hpp`
- `source/tests/post_swap_applied_root_guard_v805_tests.cpp`
- `CMakeLists.txt`

### Patch

`applyStateRootCanonical()` now treats `applyStateRootBySwap(root)` as a destructive ownership swap. Immediately after the swap it binds:

```cpp
const SidStateRootV1& appliedRoot = runtimeModel_.stateRoot();
```

All post-swap SID runtime restore now uses `appliedRoot.patch.sid_runtime`, not the swapped-out local `root.patch.sid_runtime`.

Post-swap parameter readback now uses:

```cpp
ArpSID::sidStateRootParamValueFromHydratedValuesRT(appliedRoot, paramId)
```

instead of semantic-root reads.

SID-808 factory-slot restore now decodes the slot from applied hydrated `kParamBankSlot`:

```cpp
const float appliedBankSlot =
    ArpSID::sidStateRootParamValueFromHydratedValuesRT(appliedRoot, kParamBankSlot);
const int slotInt = ArpSID::canonicalFactorySlotFromNormalizedBankSlot(appliedBankSlot);
```

### Guard

`PostSwapAppliedRootGuardV805Tests` verifies:

- `appliedRoot` is bound after swap.
- SID envelope restore uses `appliedRoot`.
- swapped-out local `root.patch.sid_runtime` is not used after swap.
- post-swap semantic parameter reads are not used.
- hydrated RT reads are present.

## 2. Closure unblock

### Files changed

- `source/tests/final_source_closure_v801_tests.cpp`
- `arpsid-fix-list-690.md`

### Patch

`FinalSourceClosureV801Tests` no longer hardcodes pass377. It derives the active pass from `VERSION.txt` and checks `version.h` and `README.md` against that derived pass.

`arpsid-fix-list-690.md` top status now reports pass380 and the active RT-only render-apply closure.

### Guards

- `StaleVersionGuardSweepV800Tests`
- `FinalSourceClosureV801Tests`

## 3. Realtime hardening: bridge transaction stack

### Files changed

- `source/au3/ArpSIDDSPKernel.hpp`
- `source/tests/render_bridge_snapshot_stack_guard_v806_tests.cpp`
- `CMakeLists.txt`

### Patch

`BridgeTransactionSnapshot` no longer embeds a full `ArpSID::C64::C64Platform`. The full legacy rollback copy is moved to persistent render-owned members:

```cpp
ArpSID::C64::C64Platform c64BridgeRollbackPlatformScratch_{};
bool c64BridgeRollbackPlatformScratchValid_ = false;
```

`beginBridgeTransaction_()` copies `player->platform()` into the persistent scratch, and `rollbackBridgeTransaction_()` restores from that scratch.

This removes the ~383 KB stack object from the audio callback while preserving the current full-platform rollback semantics. The next hardening step is a mutation journal.

### Guard

`RenderBridgeSnapshotStackGuardV806Tests` verifies:

- `BridgeTransactionSnapshot` contains no `C64Platform`.
- persistent rollback scratch exists.
- begin/rollback use the persistent scratch path.

## 4. PSID `playAddress == 0` continuous routing

### Files changed

- `include/arpsid/core/c64_psid_runtime.h`
- `source/au3/ArpSIDDSPKernel.hpp`
- `source/tests/psid_playzero_continuous_guard_v807_tests.cpp`
- `CMakeLists.txt`

### Patch

Added:

```cpp
C64RunResult runContinuousMachineCycles(uint64_t cycles,
                                        uint32_t maxInstructions,
                                        SidRegisterSink* sid) noexcept;
```

Policy:

- RSID delegates to `runRsidMachineCycles()` so strict/compatible policy is preserved.
- non-RSID images only run this path when `playAddress == 0`.
- PSID `playAddress == 0` clocks the legacy C64 realtime SID core instead of being sent to the RSID-only runner.

Kernel continuous path now calls `runContinuousMachineCycles()`.

### Guard

`PsidPlayZeroContinuousGuardV807Tests` verifies the explicit runner and kernel routing.

## 5. Strict RSID fallback truth

### Files changed

- `source/tests/strict_rsid_fallback_truth_guard_v808_tests.cpp`
- `CMakeLists.txt`

### Patch

No runtime change was needed here. The existing strict branch already refuses before legacy compatibility execution. The new guard locks that truth down.

### Guard

`StrictRsidFallbackTruthGuardV808Tests` verifies:

- strict non-PHI2 refusal exists.
- refusal increments `strictRsidNotPhi2Count_`.
- refusal returns before `++legacyRuntimePlaybackCount_`.

## 6. SID `$D41D` pseudo/system-byte routing

### Files changed

- `include/arpsid/core/sid_runtime_register_ops.h`
- `include/arpsid/engines/bitperfect_engine.h`
- `source/tests/sid_d41d_system_byte_routing_v809_tests.cpp`
- `CMakeLists.txt`

### Patch

`canonicalSidRegisterWrite(SidRegisterEngine*)` now routes register `0x1D` to:

```cpp
eng->writeSystemByte(value);
```

`BitPerfectEngine::writeSidRegister()` now routes register `0x1D` to the raw SID-register engine's `writeSystemByte()` and keeps the shadow image consistent.

`BitPerfectEngine::applySidRegisterImage()` now correctly preserves `$D41D` because it calls `writeSidRegister()` for all image entries.

### Guard

`SidD41dSystemByteRoutingV809Tests` verifies:

- canonical SidRegisterEngine write updates `currentSystemByte()`.
- canonical BitPerfectEngine write updates the raw SID-register system byte.
- BitPerfect register-image apply preserves `$D41D`.

## Validation commands run

```bash
cd /mnt/data/arp_pass380/ArpSID-0.0.690-pass380-source/build-pass380-check
cmake ..
ninja -j2 PostSwapAppliedRootGuardV805Tests RenderBridgeSnapshotStackGuardV806Tests PsidPlayZeroContinuousGuardV807Tests StrictRsidFallbackTruthGuardV808Tests SidD41dSystemByteRoutingV809Tests
ctest -R 'SidD41dSystemByteRoutingV809|V800|V801|V802|V803|V804|V805|V806|V807|V808' --output-on-failure
ctest -R 'TimingPhase2|C64Cpu6510CompleteV414|C64Phi2SidWriteBusTiming|C64Phi2RdyVectorsV606|C64CiaPhi2IntegrationV620|C64VicRasterCompareV716|C64PsidVbiCyclesV576|C64SidcoreRealtimeBootV275|C64Phi2RsidPlaybaseCycleV605|C64BootSequenceSidInitV430|C64SidPlayBootstrapV431|C64PsidRsidBootContractV739|C64CycleExactClosureV741' --output-on-failure
ctest -R 'MidiIngressRingPathV687|LogicStopStartDrsidKitV238' --output-on-failure
```

## Validation result

Passed:

```text
V800–V809 all passed.
C64 focused timing/PHI2/boot/RSID/PSID set passed.
MidiIngressRingPathV687Tests passed.
LogicStopStartDrsidKitV238Tests passed.
```

Incomplete:

```text
Full ninja build did not finish before timeout.
FactoryBankAudioAuditV687Tests started but did not finish in the command window.
macOS plugin validation was not run here.
```

## 8. C64 render mutation journal rollback

### Files changed

- `include/arpsid/core/c64_platform.h`
- `source/au3/ArpSIDDSPKernel.hpp`
- `source/tests/render_bridge_snapshot_stack_guard_v806_tests.cpp`
- `source/tests/c64_render_mutation_journal_v813_tests.cpp`
- `CMakeLists.txt`
- `REALTIME_ROLLBACK_JOURNAL.md`

### Patch

The old bridge rollback design kept the full legacy `C64Platform` copy in the DSP kernel as a persistent scratch member. This avoided a huge stack object, but still copied the entire platform during every bridge transaction.

That has now been replaced with a render-owned mutation journal inside `C64Platform`:

```cpp
beginRenderMutationJournal();
rollbackRenderMutationJournal();
commitRenderMutationJournal();
```

The journal snapshots CPU/CIA/VIC/SID scalar state and the fixed SID bus queue, while RAM and Color RAM are restored through bounded dirty-cell arrays:

```cpp
std::array<DirtyByteEntry, kMaxDirtyRam>
std::array<DirtyByteEntry, kMaxDirtyColor>
```

`ArpSIDDSPKernel::beginBridgeTransaction_()` starts the platform journal. Complete play transactions call `commitBridgeTransaction_()`. Failed/incomplete play transactions call `rollbackBridgeTransaction_()`, which restores both the SID bridge register/timed-write surface and the C64 machine side effects through the platform journal.

### Guard

`RenderBridgeSnapshotStackGuardV806Tests` was updated to require:

- no `C64Platform` member in `BridgeTransactionSnapshot`.
- no `c64BridgeRollbackPlatformScratch_` in the DSP kernel.
- bridge transaction begin/rollback/commit routes through the C64Platform mutation journal.

New runtime guard:

`C64RenderMutationJournalV813Tests`

It verifies:

- nested begin is refused.
- rollback restores RAM, CPU state and PHI2 cycle.
- duplicate dirty writes restore the original value.
- commit preserves mutations and deactivates the journal.

## Validation after V813

```bash
cmake -S . -B /mnt/data/arp_pass380_build_next -G Ninja -DARPSID_BUILD_TESTS=ON
cmake --build /mnt/data/arp_pass380_build_next --target \
  RenderBridgeSnapshotStackGuardV806Tests \
  C64RenderMutationJournalV813Tests \
  arpsid_c64_psid_bridge_transaction_v583_tests \
  arpsid_c64_psid_vbi_cycles_v576_tests
cd /mnt/data/arp_pass380_build_next
ctest -R 'V80[0-9]|V81[0-3]' --output-on-failure
ctest -R 'C64PsidBridgeTransactionV583Tests|C64PsidVbiCyclesV576Tests' --output-on-failure
```

Result:

```text
V800–V813 all passed.
C64PsidBridgeTransactionV583Tests passed.
C64PsidVbiCyclesV576Tests passed.
```

## 9. V814 state-apply ownership split

### Files changed

- `source/au3/ArpSIDDSPKernel.hpp`
- `source/tests/state_apply_ownership_split_v814_tests.cpp`
- `CMakeLists.txt`
- `TODO.md`
- `PATCHING.md`

### Patch

P1-11/P1-12 is now made explicit in code. State-root restore is split into one non-RT preparation authority and one render-side prepared-root apply authority:

```cpp
prepareStateRootForApplyNonRealtime_(SidStateRootV1& dst)
applyPreparedStateRootRT_(SidStateRootV1& preparedRoot) noexcept
```

`schedulePendingStateRestore()` now copies the incoming root into the producer-owned mailbox slot and delegates all may-allocate/canonicalization work to `prepareStateRootForApplyNonRealtime_()`. That helper owns:

```cpp
sidCanonicalizeStateRootForApply(dst);
preloadSid808FactorySlotForScheduledRestoreNonRealtime_(dst);
```

`drainPendingStateRestore_()` now calls only:

```cpp
applyPreparedStateRootRT_(*root);
```

The render helper calls `applyStateRootCanonical(preparedRoot, true)` and contains no canonicalization or SID-808 non-RT preload work. This gives future source guards a stable boundary and prevents direct mailbox-drain bypasses from reintroducing non-RT work on the audio thread.

### Guard

New guard:

`StateApplyOwnershipSplitV814Tests`

It verifies:

- non-RT preparation helper exists.
- render prepared-root apply helper exists.
- schedule path delegates preparation rather than inlining canonicalization/preload.
- drain path calls the render helper and does not bypass it.
- RT helper contains no canonicalization or SID-808 non-RT preload.
- canonical apply still reads the post-swap `appliedRoot` and hydrated values.

### Validation after V814

```bash
cmake -S . -B /mnt/data/arp_pass380_build_v814 -G Ninja -DARPSID_BUILD_TESTS=ON
cmake --build /mnt/data/arp_pass380_build_v814 --target StateApplyOwnershipSplitV814Tests
cd /mnt/data/arp_pass380_build_v814
ctest -R 'StateApplyOwnershipSplitV814Tests' --output-on-failure
ctest -R 'V80[0-9]|V81[0-4]' --output-on-failure
```

## V815/V816 — state-apply integration guards

### V815: dual SID-runtime restore authority

`SidRuntimeRestoreDualEngineV815Tests` protects the post-swap state-restore invariant for both SID-backed engines:

```text
runtimeModel_.applyStateRootBySwap(root)
const SidStateRootV1& appliedRoot = runtimeModel_.stateRoot()
BitPerfect <- appliedRoot.patch.sid_runtime
DrSid      <- appliedRoot.patch.sid_runtime
```

The swapped-out local `root` must never be used for `restorePrimarySidEnvelopeRuntimeState()` after the handoff.

### V816: SID-808 restore/flavor integration

`Sid808RestoreFlavorIntegrationV816Tests` protects the combined restore path:

```text
non-RT prepare: canonicalize + preload SID-808 factory slot
RT apply:       decode kParamBankSlot from applied hydrated root
RT apply:       consume matching preload or queue fail-safe only
RT apply:       gate bridge load by ComponentFlavor::Sid808 + SID-808 slot range
RT apply:       enforce flavor policy before backend projection
```

DrSID/DrumMachine factory slots remain engine-bank authorities and are deliberately not loaded into the SID-808 bridge.


## V817 — realtime source-lint closure

### Files changed

- `source/tests/realtime_source_lint_v817_tests.cpp`
- `CMakeLists.txt`
- `TODO.md`
- `PATCHING.md`

### Guard

`RealtimeSourceLintV817Tests` consolidates the broad source-level realtime contract:

```text
applyPreparedStateRootRT_ must not canonicalize, hydrate, ensure capacity, or preload SID-808
post-swap applyStateRootCanonical must use appliedRoot + hydrated values
DSP kernel must not reintroduce full C64Platform snapshots/scratch
render bridge transactions must begin/rollback/commit the C64Platform mutation journal
SidRuntimeModel::applyStateRootBySwap must remain RT-only
```

This does not replace the focused V804/V805/V806/V814 guards; it provides one higher-level tripwire for future refactors.

### Validation

```bash
cmake --build <build-dir> --target RealtimeSourceLintV817Tests
ctest -R 'RealtimeSourceLintV817Tests|V80[0-9]|V81[0-7]' --output-on-failure
```


## V818 — final release cleanup closure

### Files changed

- `scripts/verify_source_tree.py`
- `scripts/check_audit_closure.py`
- `RELEASE_FINAL_SOURCE_CLOSURE.md`
- `source/tests/release_cleanup_closure_v818_tests.cpp`
- `CMakeLists.txt`
- `TODO.md`
- `PATCHING.md`

### Cleanup

Removed stale active status strings from executable release surfaces:

```text
pass48-compatible
pass56 audit closure matrix
Verify ArpSID pass49 source tree guard
```

Historical docs/changelogs may still mention old pass numbers as archived history, but active scripts and release-boundary docs now speak in current-package terms.

### Guard

`ReleaseCleanupClosureV818Tests` verifies:

- current pass380 final source-closure doc;
- source-level P0/P1/P2 closure PASS marker;
- AUv2/auval/Logic/VST3/notarization remain explicitly pending Mac-platform proof;
- active scripts no longer print stale pass48/pass56 labels;
- CMake source-tree guard comment no longer carries stale pass49 wording;
- exactness/ownership/realtime rollback docs are present and carry required boundary wording.

### Validation

```bash
cmake --build <build-dir> --target ReleaseCleanupClosureV818Tests
ctest -R 'ReleaseCleanupClosureV818Tests|V80[0-9]|V81[0-8]' --output-on-failure
```

## V819 — final source closure, dead-file removal, bounded factory audio audit

Status: **applied**.

Changes:

- Converted `FactoryBankAudioAuditV687Tests` into a closure-friendly bounded audio audit by default.
  - All 180 factory slots are still checked structurally through `makeFactoryPatchStateRootForSlot()` and decoded `kParamBankSlot` identity.
  - A deterministic representative slot set is audio-rendered across synth, drum/DrSID, SID-808/DIGI-named slots and grid-spread edge slots.
  - The old exhaustive all-slot render remains available through `ARPSID_FACTORY_BANK_FULL_AUDIO_AUDIT=1` for explicit long soak/release-machine runs.
- Removed dead root-level C64 scratch/demo artifacts from the source closure:
  - `eurodance.asm`, `eurodance.prg`, `eurodance.sid`
  - `techno.asm`, `pulsegrid.prg`, `pulsegrid.sid`, `tune.bin`
  - `make_sid.py`
- Regenerated `RELEASE_CONTENTS.sha256` after removal and V819 edits.
- Added `ReleaseDeadFileClosureV819Tests` to guard the cleaned source root, manifest and closure documentation.

Validation:

```text
FactoryBankAudioAuditV687Tests       PASS  # bounded closure mode
ReleaseDeadFileClosureV819Tests      PASS
V800-V819 source closure guards       PASS
```

Full factory-bank audio soak command for release hardware:

```bash
ARPSID_FACTORY_BANK_FULL_AUDIO_AUDIT=1 ./build-debugtests/arpsid_factory_bank_audio_audit_v687_tests
```

## v845 / v845b / v845c — telemetry stall, synth filter authority, C64-auth JSON import

### v845 — heavy C64 telemetry stall fix

`publishC64PlatformMirrorTelemetry_()` treated SID register-image changes as an
unconditional heavy-snapshot state edge. During PSID/RSID playback the SID image
churns almost every block, so the expensive C64 cockpit snapshot ran on the audio
render thread every block, bypassing the demand/deadline gate. Fixed by
demand-gating the SID-image edge:

```cpp
const bool stateEdgeSnapshot = firstHeavySnapshot || phi2ResetOrWrap || psidLiveEdge ||
    (demandWantsC64Snapshot && sidImageChanged);
```

Also re-authored the factory bank toward true C64-auth: source-aware ring/sync
control bytes (internal source voices keep running), explicit `$D417` route via
`filterRouteMask`, V1/V3 sync support, register-authored synth slots. Guard:
`FactoryBankC64AuthV845Tests`.

### v845b — synth filter mode is live audio authority

synthMode factory patches render in `SidRuntimeRenderMode::SidRegister`, where the
`$D418` filter-mode bits are live audio authority. The first v845 pass reassigned
BP/HP/multi modes on those LP-tuned patches and thinned the sound. Fixed by
preserving the authored LP mode for synth patches and expressing filter-mode
variety only on the DrSID/SID808/Digi families (whose `$D418` is a cosmetic
telemetry mirror). Guard: `synthNonLP == 0` in `FactoryBankC64AuthV845Tests`.

### v845c — Classic C64 JSON import render-mode fix

`ArpSIDParseExternalC64ClassicBankDocument()` built each imported sound from a
factory seed slot (a synthMode/SidRegister patch) and overwrote only the
high-level voice/filter params. In SidRegister mode the `$D4xx` register image is
the audio authority, so the imported high-level params were ignored and the seed
slot's sound played — imported JSON sounds "did not apply". Fixes:

- non-drum classic imports are forced to **BitPerfect** mode
  (`kParamSynthModeEnable = 0`), where the imported high-level params are the
  live audio authority;
- all three VCO levels are zeroed before applying the JSON voices, so an
  unspecified voice stays silent rather than inheriting the seed slot's helper
  oscillators;
- each classic voice accepts an optional `level` (0..15).

Three example C64-auth sounds ship under `presets/` (pulse lead, punch bass,
ring bell) with `presets/README.md` documenting the Classic C64 JSON schema.

## v845d — GUI / tabs / logic audit + robustness hardening

Audited the full GUI/tab/logic surface (`ArpSIDViewController.mm` ~23.7k lines,
`tab_architecture.h`, and the KIT/MIX/DIGI panel models). Findings: the surface is
already strongly hardened — the AUv2 build is warning-free under `-Wall -Wextra
-Wpedantic`; the tab dispatch switch covers all 17 visible tabs with a
`default → Main` fallback (no blank tab is reachable); every `NSSegmentedControl`
handler bounds-checks `selectedSegment` (which is -1 when nothing is selected);
the panel models are byte-domain with `mixModelIsWellFormed`/`sanitizeMixModel`
+ `sanitizeDigiPanelModel` validation and `static_assert`-pinned layouts; there
are no `TODO`/`FIXME`/`HACK` markers and prior passes hardened all async
continuation lifetimes.

Two magic-number drift risks were hardened (no behaviour change):

- `mix_panel_model.h`: added `kMixFxTypeCount` + `mixFxTypeIsValid()` and derived
  the well-formedness bound from them instead of the literal `> 5u`, with a
  `static_assert` tying the count to the `MixFxType` enum. Adding a future FX type
  can no longer make the validator silently reject it. Guarded by new assertions
  in `MixPanelModelV547Tests`.
- `ArpSIDViewController.mm`: the drum quick-kit shortcut's per-flavor parallel
  tables (slots / popup indices / labels) now derive every bound from a single
  `kArpSIDDrumQuickKitSegments` constant with a `static_assert` per table,
  preventing parallel-array drift where one table changes length and the stale
  `idx >= 6` bound reads out of range.

Note (surfaced, not changed): the Sid808 quick-kit segment slots
`{120,121,118,119,123,124}` intentionally mix SID-808 and DrSID slots as a curated
6-favorite set (the labels `808/DUB/WIDE/CYM/TRK/TRACE` do not map 1:1 to the five
SID-808 kits), so it is left as authored.

### v845d.1 — `TemplateBlobMailboxOwnershipV749Tests` parallel-load flake fix

The concurrent fast-producer/slow-consumer stress in
`template_blob_mailbox_ownership_v749_tests.cpp` intermittently failed its
terminal `lastSeq == kPublishes` assertion under saturated `ctest -j` load. Root
cause was in the TEST, not the mailbox: the consumer's post-`done` drain branch
consumed a blob via `tryConsume()` but never decoded/recorded its sequence, so
when the final blob (`seq == kPublishes`) surfaced in that branch — a window that
widens under load — the last recorded sequence stayed below the total and the
check flaked. Fixed by routing every consumed blob (including the post-`done`
drain) through one sequence-recording path. Verified by contrast: the pre-fix
consumer fails within a 12-way-concurrent wave, the fixed consumer passes 120/120
under the same harness.

### v845d.2 — `RenderEpochAndScopeTripleBufferV521Tests` parallel-load flake fix

Uncovered while re-running the full suite under `-j 8`: the concurrent
render-epoch section required BOTH held and broken outcomes to appear across 5000
iterations of a renderer thread racing a bumper thread
(`require(heldCount > 0)` / `require(brokenCount > 0)`). Under saturated load the
scheduler can starve either thread, driving the split to all-held or all-broken,
so one `require` aborted. A specific concurrent interleaving cannot be asserted
portably. Fixed by keeping the deterministic contention invariant
(`heldCount + brokenCount == 5000` — every iteration yields exactly one
well-defined decision, no torn/lost epoch verification) and proving both outcomes
are reachable deterministically (single-threaded: one scope with no bump →
`held()`, one scope with a mid-scope bump → `!held()`). Fixed test passes 120/120
under 12-way concurrency and the full suite passed 5/5 consecutive `-j 8` runs
(371/371 each).

## v846 — C64 state / telemetry realtime-safety audit

Audited the whole C64 state + telemetry surface for realtime-safety on the audio
render thread (`c64_telemetry.h`, `c64_platform.h`, `c64_psid_runtime.h`,
`arpsid_telemetry_snapshot.h`, `diagnostic_snapshot.h`, and the kernel telemetry
path in `ArpSIDDSPKernel.hpp`).

Findings — the subsystem was already largely RT-safe:

- `C64ChipSnapshot` is fixed-size POD (`std::array` members, no heap); the
  `C64TelemetryGate` publishes through a lock-free triple-buffer seqlock.
- No allocations, mutexes, `std::string`, `std::vector`, or blocking calls on the
  render/publish path; the kernel render path has zero `snprintf`/`std::string`.
- The heavy cockpit snapshot is demand-gated to ~60 Hz (v845), and its scalar
  atomics + bus scope run per block with bounded work only.

One genuine RT-safety weak point fixed:

- `c64FormatDisassemblyLine()` formatted three disassembly lines with
  `std::snprintf` during the heavy snapshot build (render thread). `std::snprintf`
  can take a per-call locale lock in some libc implementations, so it is not
  guaranteed realtime-safe. Replaced with a deterministic, allocation-free,
  locale-free hex formatter; the legacy 32-char mirror now uses the existing
  bounded `c64CopyFixedAscii` instead of `snprintf("%s")`. No `snprintf` remains
  in `c64_telemetry.h`. New guard `C64DisasmRtFormatV846Tests` proves the RT-safe
  formatter is byte-identical to the previous snprintf layout across all 256
  opcodes and a spread of PC/operand values (55,296 combinations).

## v847 — poly stuck-note fallback (fast chords / voice stealing)

Reported: some notes stick during fast poly chords on the BitPerfect/SID synth.
Audit: the poly note path is heavily guarded (token identity, held-ingress
mirroring, dropped-note-off latching, silent-voice reclaim), but the strict
matcher `VoiceManager::noteOffDetailedResult()` deliberately refuses to let an
anonymous NoteOff (noteId<0) release a voice that carries a real host noteId —
correct for same-note polyphony, but it leaves a STUCK GATE when a host/AU path
does not round-trip note ids symmetrically, and BitPerfect's poly note-off then
did nothing on a non-match.

Fix: added `VoiceManager::noteOffLooseSameNote(note, channel)` and call it from
BitPerfect's poly note-off only when the strict match finds no voice. It releases
the oldest still-key-down voice for the same note+channel, ignoring noteId, and
still honours the sustain/sostenuto pedals. Because it fires only after the strict
match already failed, it cannot change any correct-match case — it just guarantees
a note-off can never leave a stuck gate. Guard: `PolyStuckNoteLooseReleaseV847Tests`
(asymmetric-noteId release, pedal-hold preserved, note/channel scoping, clean
strict path leaves nothing to release).

## v852 — poly stuck-note reconciliation (definitive safety net)

After v847 a user still reported occasional stuck notes. Root of the remaining
cases: the strict/loose note-off matchers can still miss when a note-off never
reaches the engine with a compatible identity at all — e.g. an anonymous note-off
during arp on/off handoff (the `noteId >= 0` guard in `runtimeHandleRenderedNoteOff`
skips the safety-net BitPerfect release for MIDI-1.0 notes), a channel mismatch,
or a genuinely dropped event. When no match is found the gate is simply left on.

Added a per-block reconciliation that makes the invariant explicit: the set of
gated poly voices may never outlive the set of physically-held keys. Each block,
after the synth render, `BitPerfectEngine::reconcileUnheldPolyVoices` (backed by
`VoiceManager::reconcileUnheldVoices`) releases any key-down poly voice whose
(channel, note) is reported NOT held by the kernel's authoritative held-ingress
mirror and NOT held by a pedal, dropping its SID gate.

Safety:
- Only pure DIRECT polyphony is reconciled. When the arpeggiator, sequencer or
  DrSID is active — which generate voices that are not in the MIDI held set —
  every key reads as "held" and nothing is touched.
- Pedal (sustain/sostenuto) held voices are preserved.
- A two-consecutive-pass grace period means a note-off landing exactly on a buffer
  boundary is never clipped; a genuinely stuck voice stays orphaned indefinitely
  and is released on the next pass.

Guard: `PolyOrphanReconcileV852Tests` (grace period, pedal preservation, all-held
preservation, grace reset on lapse, explicit reset). RT-safe: fixed-size, no
allocation, ≤8-voice scan plus a handful of relaxed/acquire atomic loads.

## v853 — music/math/timing/6510 audit + exhaustive opcode/cycle guard

Audited the music/timing/emulation math and the 6510 core for flaws:

- Clocks: PAL PHI2 985248 Hz / NTSC 1022727 Hz correct; CIA TOD 50/60 Hz periods
  keyed to the right clocks.
- Tuning: MIDI→Hz is `440·2^((n−69)/12)` (12-TET, A440), detune in true cents
  (`2^(cents/1200)`), pitch bend in semitones — correct.
- SID oscillator: literal 24-bit phase accumulator; noise LFSR uses the
  hardware-correct 23-bit register with bit22^bit17 feedback, clocked from phase
  bit 19 (delta-based, wrap-safe), zero-state escape; the noise DAC maps LFSR
  bits {22,20,16,13,11,7,4,2}→output bits 11..4 with the authentic dead low
  nibble; pulse comparator is the standard `t >= pw` 12-bit law. No flaws found.
- 6510: empirically enumerated ALL 256 opcodes through the bus-cycle-accurate
  `Cpu6510Micro` — 244 execute, exactly the 12 documented KIL opcodes halt,
  ZERO unsupported-opcode gaps. Verified documented cycle counts for 19
  instruction/addressing-mode cases plus the abs,X page-cross +1 penalty, branch
  not-taken/taken/taken-page-cross (2/3/4), and BRK = 7 cycles through $FFFE.
  The core was already complete ("all 151 official + stable NMOS illegals",
  approximate ops policy-gated for strict mode); it is now pinned by the
  exhaustive guard `Cpu6510MicroOpcodeMatrixV853Tests` so it cannot regress.

Viz: the header oscilloscope trace now follows the active tab's accent colour
(same palette as the v850/v851 per-tab badge), so the whole header chrome
coherently identifies the active function.

## v854 — ultra-low-level SID core audit (three exactness fixes)

Line-by-line audit of the SID chip core (`sid_chip.h`, `sid_envelope_core.h`)
against MOS 6581/8580 die-analysis behaviour (reSID reference). Verified correct:
24-bit accumulator; TEST-bit hold/release; hard-sync topology V1<-V3/V2<-V1/V3<-V2
with the sync-of-sync non-propagation exception; triangle bits 22..11 with
ring-mod MSB XOR from the source voice; noise 23-bit LFSR (bit22^bit17 feedback,
phase-bit-19 clocking, wrap-safe delta stepping, zero-state escape, dead low
nibble in the DAC mapping); OSC3 readback = top 8 waveform bits; envelope FSM
(attack→decay on the 0xFF event, sustain clocked at decay rate, freeze at zero,
6581 ADSR-delay-bug counter preservation, 46-cycle hard-restart discharge).

Three genuine hardware mismatches found and fixed:

1. `kRatePeriods[15]` was 31250; the die-measured LFSR period for rate 15 (8 s)
   is **31251**. Envelope rate 15 was one cycle fast.
2. `kExpoDivByLevel` used `>=` at the 93/54/26/14/6 boundaries. On hardware the
   exponential-counter period latches when the counter REACHES a boundary, so the
   decrement FROM level 93 already costs divisor 2. Correct map:
   [94..255]→1, [55..93]→2, [27..54]→4, [15..26]→8, [7..14]→16, [0..6]→30.
   Each boundary level previously decayed one divisor bucket too fast.
3. `generatePulse12()` special-cased PW=$FFF as constant silence. The hardware
   comparator `t >= $FFF` is true exactly when the accumulator's top 12 bits are
   $FFF — a 1/4096-duty spike train. Removed the special case; the general
   comparator now renders it exactly (PW=$000 stays constant high, and the 6581
   high-width comparator bias clamps safely).

All engines share this single envelope/waveform core, so the fixes cover the
BitPerfect, SidRegisterEngine and DrSID render paths. Intentional deviation left
as designed (documented, guard-tested elsewhere): `onSustainChanged()` raises the
envelope to a raised sustain target for musical automation, where real hardware
can only decay (raising SR mid-sustain on a real SID decays to zero).

Guard: `SidCoreExactnessV854Tests` — pins the 31251 attack-15 step cycle, all five
exponential divisor boundaries (level 94/93, 55/54, 27/26, 15/14, 7/6 tick-exact
in Release at rate 0), PW=$000/$800/$FFF duty counts over a full 4096-step sweep,
sync topology, and live noise-LFSR readback. Full suite 379/379.

## v855 — PSID play-write audio-bridge routing (P0 timing fix) + audit closure

Validated external audit finding, now fixed: the audio renderer consumes SID
writes from `c64SidBridge_.timedWrites`, but the discrete PSID VBI and PSID-CIA
play paths attached only the internal runtime sink
(`phi2SidBridge_.attach(&sink_, nullptr)`). Play-routine SID writes therefore
updated the platform SID mirror but never reached the audio bridge — audible
updates were quantized to block edges (up to ~11.6 ms at 512 frames / ~23.2 ms at
1024 frames @ 44.1 kHz). This split-brain path is the primary cause of the
remaining "small timing errors / glitchy SID" symptom.

Fixes:
- `C64Runtime::runPlay(maxInstructions, SidRegisterSink* externalTimedSink)`,
  `runPsidVbiPlayViaPhi2_(...)`, `runPsidCiaPlaybackServiceTicks(...)` and the
  RSID frame path inside runPlay now thread an external timed sink into
  `phi2SidBridge_.attach(&sink_, externalTimedSink)`. The kernel passes
  `&c64SidBridge_` from both the VBI and CIA render paths, so play writes land
  at their exact PHI2-derived sample offsets.
- P0.3: the CIA commit condition now uses the runtime's full completion proof
  `service.serviceComplete` (play entered + CIA ACK + returned-to-idle + not
  jammed) instead of `playAddressEntered && ciaAckObserved`, which could commit a
  half-finished play frame (missing gate-offs, half-updated filter/frequency).
- P0.4: `BridgeTransactionSnapshot` now captures/restores the runtime sink's
  register banks (192 bytes fixed-size), so a rolled-back play cannot leave
  partial writes that a later successful `syncPlatformSidMirrorFromSink_`
  republishes.
- P1.4: `C64::SidReadbackModel` PW=$FFF pulse readback fixed to the hardware
  1/4096-duty spike (parity with the v854 SIDVoice fix — $D41B OSC3 was still
  constant low at max width).
- P1.5: v854 test scope wording corrected — SIDVoice exactness is not C64
  bus/readback exactness; the C64 surface now has its own guard.
- P1.6: all render-thread `std::chrono::steady_clock::now()` calls are gated
  behind `ARPSID_ENABLE_RENDER_PROFILING` (default OFF → zero wall-clock reads
  on the audio thread; phase timings read 0 µs when gated).
- P1.7: `processBlock()`/`processBlockMono()` normalize `events==nullptr` /
  negative `eventCount` at entry (no null dereference from a host bridge).
- P1.1/P1.2: PSID VBI cadence policy made explicit in `c64_timing_math.h` —
  VBI = physical VIC frame (PAL 19656 ≈ 50.1245 Hz), CIA = compatibility
  50/60 Hz latch (PAL 19705); close but NOT the same number, never mixed.
- P2.1/P2.2: `strictRealtimeNotifyMode` documentation corrected (diagnostic/
  attribution only — it does not quarantine AUv2 notify dispatch, which the AU
  contract requires); `renderNotifySkippedStrictCount` renamed to
  `renderNotifySkippedUnstablePublicationCount` (it counts seqlock-unstable
  publication skips, not strict-mode skips).
- P2.4: non-Apple `hostTicksToSeconds_()` now returns a negative sentinel and
  all three call sites treat it as UNRESOLVED (sampleOffset −1) instead of
  silently collapsing host-timed events to sample offset 0.

Guard: `C64PlayBridgeRoutingV855Tests` — proves VBI play writes arrive in the
external bridge (count parity with the internal sink, monotonic PHI2 stamps,
both fixture registers present; unattached bridge stays empty), CIA service
writes arrive with `serviceComplete == true`, and `$D41B` OSC3 readback renders
PW=$FFF as a 1/4096 spike and PW=$000 as constant high. Full suite 380/380.

Honest remaining boundaries (documented, not hidden): the PHI2 machine's VIC
bus-steal is a half-cycle contention approximation; CIA/open-bus/ROM-identity
approximations remain per `C64_EXACTNESS_BOUNDARIES.md`. Release wording is
"SID-core / PHI2-timed playback improved", not "full physical C64 exactness".

## v856 — missing PSID/RSID logic for "still choppy on some SIDs"

Systematic audit of the full PSID/RSID tune-class matrix found three missing
pieces of logic, each hitting a specific tune class hard (which is why only
"some" SIDs stayed choppy after v855):

1. **Multi-speed CIA cadence (kernel)**: the CIA-path play cadence used the fixed
   50/60 Hz default latch (PAL 19705). Tunes that reprogram CIA Timer A
   ($DC04/$DC05) for 2x/4x/tracker tempos were serviced at half/quarter of their
   intended rate. The kernel now reads the LIVE CIA1 Timer A latch from the PHI2
   machine every block (clamped ≥1000 cycles; play calls stay bounded by
   kC64PsidMaxPlayCallsPerAudioBlock), so multi-speed and mid-song tempo changes
   follow the tune.
2. **Tune-programmed CIA tempo preservation (platform)**:
   `installPsidCiaPlaybackBootstrap()` runs AFTER the tune's init and
   unconditionally wrote the 50/60 Hz default latch, CLOBBERING a tempo the
   tune's init had programmed. The CIA reset latch is $FFFF, so a non-$FFFF
   latch after init proves tune programming — it is now preserved and the
   default installed only when init left the timer untouched. (The init CIA
   state reaches the platform via syncPlatformInspectionFromPhi2_ before the
   install, and the preserved latch propagates back to the PHI2 machine via
   syncPsidCiaBootstrapIntoPhi2_.)
3. **RTI-exiting play routines (runtime)**: the VBI dispatch is JSR play / JMP
   halt, which only an RTS-exiting play unwinds. The large PSID class that exits
   play with RTI (written for IRQ-entry players) consumed the 2-byte JSR frame
   plus one garbage byte as an IRQ frame — guaranteed jam/budget-out, rolled
   back by the kernel on EVERY frame: audibly choppy/silent for the whole class.
   The VBI runner now detects the exact RTI-exit signature (RTI retired with
   SP == entrySP+1, wrap-safe, after at least one real play instruction) and
   accepts the frame; tune-internal nested RTIs never match.

Guards added to `C64PlayBridgeRoutingV855Tests`: an RTI-exit fixture completes
8 consecutive frames each delivering its SID write to the audio bridge; a
100 Hz-programming init fixture keeps latch $267C through the bootstrap install
while an untouched-timer control still gets 19705. Full suite 380/380.

## v857 — write-through/RAM/808/transport audits + SID format docs

Five-part audit pass:

1. **Write-through memory + known C64 hacks — audited, already correct.**
   `decodeCpuWrite` routes every non-I/O CPU write to RAM (ROM is never a write
   target): writes under BASIC/KERNAL always hit the RAM beneath; Ultimax gating
   handled; $00/$01 processor-port writes also write through to RAM; I/O-banked-
   out $D4xx stores go to RAM (counted); $DC0D/$DD0D read-to-ack honoured.
   Existing coverage: `c64_phi2_processor_port_tests`, `C64AuditClosureV608`.

2. **RAM values update non-realtime-initiated — verified by design.** The C64
   memory windows are copied into the heavy snapshot ONLY while a C64-facing
   pane (C64 / SIDCORE / C64 STATE) polls: GUI poll → `noteC64TelemetryRequest`
   demand serial → render copies 8×64 bounded bytes during the demand hold at
   the deadline cadence. No visible pane → zero RAM copying on the audio thread;
   the GUI never reads live runtime memory directly.

3. **SID format documentation added**: `docs/SID_FILE_FORMAT_NOTES.md` — full
   header layout, per-song speed bits, flags (video/model), RSID constraints,
   init/play conventions incl. RTS/BRK/RTI exits, multi-speed CIA behaviour,
   write-through and the other honoured hardware behaviours, multi-SID v2NG–v4
   addresses, with implementation cross-references.

4. **SID-808 audit — one real logical bug found and fixed (v857)**: Tom maps to
   SID voice 0 (`sid808VoiceForDrum`) but carried `ChokeGroup::Cymbal` — the
   voice-2 group — so a Tom hit spuriously choked a ringing Cowbell/Hat on a
   DIFFERENT physical voice (and vice versa), with a stale "tom+cowbell share
   voice 2" comment documenting an outdated mapping. Tom now uses
   `ChokeGroup::TomShared` (toms choke toms only, x0x-correct); the voice-0
   collision with Kick resolves via the forced-voice reprogram like every other
   same-voice pair. Voice reservation (kick/tom→0, snare/clap/rim→1,
   hat/cowbell→2), accent threshold, velocity-0-as-note-off and hat choke law
   verified correct.

## v860 — SID-808 "noise when pressing pad" — two-part root cause, measured + fixed

Empirically probed with a full-kernel harness (Sid808 flavor, kit 120,
transport-stopped simulation like Logic):

1. **Constant DC floor from load**: the SID-808 bridge REPLACES the whole output
   bus, and the SID model's D418 master-volume DC level (measured **+0.0196**
   constant, AC ≈ 0.00007) went straight to the host, bypassing every canonical
   DC-handling stage. Every gate/bypass/pad edge stepped that DC — the audible
   clicks/crackle. Fixed with an RT-safe one-pole DC blocker (~4 Hz high-pass,
   two state floats per channel, state reset when the bridge is inactive) on the
   bridge output; drums pass unchanged (probe: idle now settles to silence).
2. **The pad's drum never actually played in Logic**: `suppressHostNoteOns`
   gates ALL host note-ons while the transport is known-stopped — only DIGI pads
   had a carve-out. Pressing an 808 pad with Logic stopped triggered NO drum at
   all; the user heard only the DC clicks ("just gives noise"). GM drum-channel
   (ch10) notes now bypass the transport suppression at both dispatch sites,
   exactly like the DIGI carve-out — drum pads audition with the transport
   stopped, synth/arp notes remain suppressed. `DigiMidiPadTransportStopV745Tests`
   updated to the new guarded-branch shape.

Probe results after the fixes (Logic-stopped simulation): load = silence
(DC settles), ch10 pad kick = clean periodic drum (peak 0.20, 28 zero-crossings),
direct trigger = full kick (peak 0.65). Full suite 380/380.

## v859 — SID-808 noise-on-load, double render, stale kit identity

Three user-confirmed defects fixed:

1. **Noise on load (root-caused)**: `follow_host_tempo_seq` defaults to false, so
   the internal seq clock FREE-RUNS regardless of host transport, and the SID-808
   factory defaults set `kParamSeqEnable = 1` with a gated 16-step pattern —
   loading any 808 kit started playing immediately. Kits now load with the
   sequencer OFF (silent until the user plays pads/MIDI or enables SEQ). On top,
   the default pattern was a GM-coverage TEST pattern (one hit of every drum
   cycling — the "default kit/mapping is wrong" sound); each kit variant now
   ships its authored musical groove (Classic 4-on-the-floor, Punch, sparse
   Lo-Fi/dub, Hard/tracker, Wide/breaks — recovered from the retired per-slot
   table), so enabling SEQ plays the kit's real pattern. Contract tests
   `AuthenticBassSid808LogicV241` and `Sid808FactoryDefinitionV504` updated to
   pin the new silent-until-played behaviour.
2. **DrSID bridge double render**: in Sid808 flavor, `runtimeTriggerDrSidNote`
   fired BOTH the canonical DrSID engine and the SID-808 bridge, and the bridge
   then REPLACED the whole output bus — a complete canonical DrSID render was
   computed and thrown away every block (plus a wrong-sounding DrSID ghost on any
   path where the replacement did not run). Sid808 flavor now dispatches ONLY to
   the bridge once its SID-808 identity is active, with the canonical engine as
   the documented FAIL-OPEN path (notes can never fall silent). Contract test
   `DrumBridgeNoSilenceV613` updated to pin bridge-only dispatch + fail-open.
3. **Stale/wrong kit on reset/direct-MIDI**: the RT identity repair hard-coded
   factory slot 120 (Classic), so direct MIDI after loading kit 137 — or after a
   reset cleared the bridge identity — snapped the kit identity back to Classic.
   Both repair sites now decode the CURRENTLY LOADED bank slot from renderParams
   (`sid808IdentityRepairSlot_`, clamped to the 120..149 SID-808 range) so the
   repaired identity matches the loaded kit; the SID-808 filter/param projection
   then syncs against the right identity.

Full suite 380/380 after the contract updates.

## v858 — SID-808 100% audit: GM/MIDI, banks, kits, defaults, GUI, split-brain

Full-surface SID-808 audit results:

- **GM/MIDI mapping — correct and complete.** MIDI note → `sidGMDrumClassForNote`
  (spec table, notes 35..81 all classed) → `sid808DrumFromGmClass` — a clean 1:1
  bijection over all 8 drum classes with explicit Unsupported handling, plus the
  inverse `gmDrumClassForSid808` for cross-engine identity. Per-note tune/pan/
  decay scalars come from the same spec table.
- **Banks/kits — single authority, consistent.** Slots 120..149 = 5 kit families
  repeated via `(slot-120) % 5`, identical modulo in `makeSid808KitDefinition`
  and `factorySid808KitForSlot`. `factorySid808ParamSignatureForSlot` is DERIVED
  from the kit voice-config table (`factory_sid808_kits.h`) — parameters cannot
  drift from the engine tone configs.
- **Defaults — live path verified.** `applyFactorySid808MissingLogicDefaults`
  publishes the full authored x0x voice page + per-kit swing/reverb/limiter/
  filter + 16-step GM coverage pattern for every SID-808 slot.
- **GUI — audio authority routed correctly.** The drum HUD quick-kit loads go
  through `_deferFactoryDrumKitApplyForSlot` (the factory-slot apply path), so
  HUD kit selection and BANK slot selection produce the same audio; descriptor
  tables are presentation/audition-pattern only.
- **Split-brain FOUND and removed (the one real defect).**
  `applyFactoryDrSidDefaults()` still contained per-slot cases 120..124 and 127
  ("Analog X0X-8", "Dub Chips", "Crunch Rock", "Tracker GM", "Forensic Kit",
  "Gunshot") carrying kit tunings and sequencer pages that DIVERGED from the
  live SID-808 authority — including a misleading "must match the authored
  SID-808 UI descriptor exactly" comment. All six cases were DEAD CODE:
  `factorySlotContext(120..149) == SID808_AnalogProjection` (static_assert-
  pinned) makes the function early-return for those slots, so the divergent
  table could never execute but invited edits to the wrong place. Removed
  (~150 lines) and replaced with a pointer to the live authority. Behavioural
  no-op proven: `FactoryPatchApplyRoundtripV850Tests` (11,340 values) and
  `FactoryBankC64AuthV845Tests` produce identical results before/after; full
  suite 380/380.

5. **Transport state audit (all modes) — comprehensive, no defects.**
   `handleTransportDiscontinuity_` handles start (runtime clear + beat-seeded
   arp PRNG), stop (all-notes-off reaching BitPerfect+DrSID+ARP+both DIGI
   engines via `runtimeRenderHostAllNotesOff`, transient reset, sequencer
   phase/step reset), and seek with a well-reasoned polling-jitter tolerance;
   sequencer restart arming and arp phase rewind on all three edges. The C64
   SID player is deliberately not transport-gated (it is a tune player).

## v848 — drum GM key-map GUI + UX/HUD

The canonical GM percussion note->name->SID-class table already existed in
`sid_gm_drum_kit.h` but nothing surfaced it to the user. Added
`arpsid/gui/gm_drum_map_reference.h` (`gmDrumMapReferenceText()` full 35..81 key
map, `gmDrumPadTooltip()` per-pad) and wired it into the drum tabs:

- DrSID VOICE TRIGGERS pads now show the GM note number on the pad face
  ("KICK 36", "SNARE 38", …); section retitled "VOICE TRIGGERS · GM CH10".
- Each pad's tooltip shows the full GM instrument name + SID drum class for its
  note; the trigger section, the DrSID footer HUD, and the SEQ drum HUD all expose
  the complete GM key map (notes 35..81) on hover.

Guard: `GmDrumMapReferenceV848Tests`. Combined with the v847 stuck-note fix, this
closes the reported drum-mapping/UX gaps.

## v849 — low-level PSID/RSID audit + robustness hardening

Audited the low-level RSID/PSID path (`psid_header.h` parser, `c64_psid_runtime.h`
load/init/play, and the kernel's PSID load) for playback blockers. Findings — the
subsystem is already strongly hardened: the header parser bounds every read and
validates magic/version/offset/song/RSID/extra-SID fields; the init/play CPU runs
under an instruction budget (no audio-thread hang); the kernel clamps the selected
subtune, extracts PAL/NTSC (flags bits 2-3) and SID model (bits 4-5) with bounded
reads, and RSID BASIC-startup tunes are rejected fail-clean rather than executing
BASIC as machine code. No crash/hang/over-read blocker was found.

Two defensive fixes were made:

- `psidParse()` now rejects a degenerate header-only file (payload length 0 —
  `dataOffset == len`, or a load-address-only image with no code) with `TooShort`,
  instead of returning OK and letting init execute uninitialised RAM.
- `C64Runtime::runInit()` now clamps the selected subtune to `[1, songs]`
  internally (defensive-in-depth; the kernel already clamps, but the public entry
  point must never init a non-existent song).

New guard `PsidParserRobustnessV849Tests`: fuzzes the parser across every
truncation length and field corruption (pinning the exact rejection codes),
throws 4,000 random buffers at it with no crash, and proves `psidLoadIntoRam()`
clamps writes to the 64 KiB RAM window (no write past `$FFFF` or below the load
address).

## v850 — GUI audit: per-tab logo, options-wired verification, patch-apply guard

### Per-tab brand logo

Made the persistent header brand badge (`ArpSIDC64LogoView` / `_headerLogoView_v687`)
tab-aware: it now draws the active tab's canonical HUD label
(`tab_architecture.h`) in a distinct per-tab accent colour, updated from
`_showTab:`. Reuses the badge's existing (known-safe) header frame, so it is a
custom per-tab/function logo with zero layout risk.

v851 follow-up: added a bespoke drawn vector glyph per tab (`ArpSIDDrawTabGlyph`)
rendered next to the accent label in the tab's colour — sine (MAIN), arp stairs
(LFO/ARP), register grid (SID REG), step row (SEQ), drum+stick (DRSID), low-pass
slope (FILTER), knob (MACRO), magnifier (FORENSIC), IC chip (SIDCORE), breadbin
(C64), speaker (HI-FI), stacked cards (BANK), gear (OPTIONS), sliders (SETTINGS),
faders (MIX), pad grid (KIT), and a 4-bit stair wave (DIGI). So each tab/function
now has its own colour + icon + label in the header badge.

### Options linked/enabled audit

Audited every disabled/conditional control in the editor. All are proper state,
not dead widgets: `_modePop`, the DIGI keep/discard/paste/trim buttons,
`_digiAuthModeLegacyButton`, and `_drumUserKitPop` each disable conditionally and
are re-enabled by their refresh paths; every action method has a real body; there
are no `TODO`/`placeholder`/`not-wired` controls. No dead options were found.

### Factory patch load/apply — deeper audit

Traced factory slot selection -> `makeFactoryPatchStateRootForSlot()` -> apply.
Added `FactoryPatchApplyRoundtripV850Tests`, which proves that **every**
audio-authority parameter the factory authors for a slot survives into the applied
state root (no silent drop between load and apply): 11,340 audio-authority param
values across all 180 slots round-trip within tolerance. Program/bank identity
params (canonicalised) and runtime-only params (reset by design) are excluded.
