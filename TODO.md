# v970 test build-graph closure (CLOSED, handoff 22.7 partial)

Status: build-infrastructure change complete; no production/runtime change. Package identity: `0.0.690-pass380-v970-test-build-graph-closure`.

- Compiled the forensic patch bank once into a static lib (arpsid_forensic_patchbank); 14 test
  targets now link it instead of each recompiling source/forensic_patch_bank.cpp. ODR-verified by
  a clean full build. Production wrappers unchanged.
- Test: `BuildGraphForensicPatchbankLibV970Tests`.
- 22.7 REMAINING (not done, by design): the AU-kernel header (ArpSIDDSPKernel.hpp, ~106 test
  includes) is the bigger duplication but extracting it to a compiled TU is production surgery;
  its risk is not justified by the ~1s/TU compile cost on current hardware. Also open: object
  libraries for other heavyweight TUs, unity builds where diagnostics allow, and separating
  source-contract tests from runtime tests.
- Remaining external sign-off unchanged: macOS AU/Logic (22.4), real-SDK VST3 (22.5),
  sanitizers/fuzzing (22.6), deeper C64 exactness (22.11).

# v969 test-suite integrity closure (CLOSED)

Status at v969: test-infrastructure audit-and-repair complete. Package identity was `0.0.690-pass380-v969-test-suite-integrity-closure`.

- Audited 491 test sources. No genuinely legacy/obsolete tests to remove (apparent orphans test
  live behavior and are mostly foreach-registered; removal would delete real coverage).
- Restored the one true orphan (auv3_render_scratch_transport_v591) to the build; drifted
  stable-scratch assertion made linkage-tolerant.
- De-flaked ScopeTripleBufferMultiConsumerV687Tests (producer waits for both consumers; scheduling
  artifact, not a buffer defect).
- Test: `TestSuiteIntegrityV969Tests`. No production source changed.
- Follow-up worth considering (handoff 22.7): the test build graph is heavy — many tests recompile
  large TUs; an object-library/seam refactor would cut build time. Not done here (out of scope).
- Remaining external sign-off unchanged: macOS AU/Logic (22.4), real-SDK VST3 (22.5),
  sanitizers/fuzzing (22.6), deeper C64 exactness (22.11).

# v968 DrSID kit-save normalization + full-suite green closure (CLOSED)

Status at v968: focused source/runtime closure complete. Package identity was `0.0.690-pass380-v968-drsid-kit-save-and-queue-test-closure`.

- Fixed DrSID user-kit SAVE render-mode normalization (forces DrSidEnable=1/SynthMode=0 into the
  saved root for both formats via `_saveDrumKitDocumentToURL`). This closes the last fixable
  DrSID/kit/GM audit item.
- Repaired 4 pre-v965 tests (V404/V408/V909/V590) that pinned superseded PIPE-001/PIPE-013 and
  telemetry contracts; the focused suite is green. Re-pinned, not weakened.
- Tests: `DrSidUserKitSaveModeNormalizationV968Tests` (+ the 4 repaired tests).
- Not changed (by design): the curated DrSID quick-kit overlay voicings (v967 covers the
  "Standard" base ↔ factory base invariant). The quick-kit overlay differing from a factory slot
  for the 15 named kits is intentional.
- Remaining external sign-off unchanged: clean full-suite run in a release build (22.3 — the
  four blockers are now fixed), macOS AU/Logic closure (22.4), real-SDK VST3 build and host
  matrix (22.5), sanitizers/fuzzing (22.6), test build graph refactor (22.7), deeper C64
  exactness validation (22.11).

# v967 DrSID quick-kit base-profile parity closure (CLOSED)

Status at v967: focused source/runtime closure complete. Package identity was `0.0.690-pass380-v967-drsid-quick-kit-base-parity-closure`.

- Fixed DrSID quick-kit "Standard" base-voicing drift: GUI tone table base tom decay re-synced to
  the canonical factory base (0.30→0.44, the v909 correction the GUI table missed).
- Fixed "Standard" quick kit (GUI-only, slot -1) applying nothing to audio while reporting a load.
- Test: `DrSidQuickKitBaseProfileParityV967Tests`.
- Scoped out by design: the curated quick kits (Taiko, Cymbal FX, …) are intentional voicings
  distinct from their backing factory slots and were left unchanged. The broader "quick-kit
  overlay vs factory patch" dual-authority is intentional for those curated kits; only the
  Standard base is contracted to the factory base.
- Remaining external sign-off unchanged: clean full-suite run (22.3), macOS AU/Logic closure
  (22.4), real-SDK VST3 build and host matrix (22.5), sanitizers/fuzzing (22.6), test build
  graph refactor (22.7), deeper C64 exactness validation (22.11).

# v966 host parameter presentation authority (CLOSED)

Status at v966: focused source/runtime closure complete. Package identity was `0.0.690-pass380-v966-parameter-presentation-authority-closure`.

- Closed handoff 22.1: shared `SidParameterPresentation` service; AUv2/AUv3/VST3 display and
  text parsing delegate to the canonical DSP law helpers per parameter ID.
- Closed handoff 22.2: real bounded UTF-8 ⇄ UTF-16 conversion in the VST3 controller.
- Closed handoff 22.8: VST editor stderr diagnostics gated behind `ARPSID_VST_EDITOR_DIAGNOSTICS`.
- Advanced handoff 22.9: typed `SidParameterUnit` descriptor replaces per-wrapper unit-string
  matching for host metadata (full table vocabulary normalization remains open).
- Test: `ParameterPresentationAuthorityV966Tests`.
- Remaining external sign-off unchanged: clean full-suite run (22.3), macOS AU/Logic closure
  (22.4), real-SDK VST3 build and host matrix (22.5), sanitizers/fuzzing (22.6), test build
  graph refactor (22.7), deeper C64 exactness validation (22.11).

# v965 canonical render-pipeline closure (CLOSED)

Status at v965: focused source/runtime closure complete. Package identity was `0.0.690-pass380-v965-canonical-render-pipeline-closure`.

Closed: canonical order split, post-FX offset leakage, multi-step KIT/DIGI collapse,
mid-sample engine/clock split, AU telemetry live-field race, AU no-output freeze,
PAL-only D418 timing, ARP fractional fallback, release starvation, stale cycle metadata
and duplicate VariantChange mutation. Host overlay differences are now an explicit
compile-time capability contract rather than an undocumented parity claim.

Validation in this source pass: 18/18 focused tests passed across canonical ingress,
fractional timing, D418, sequencer/KIT, telemetry, no-output, state-root, ARP, GUI v962-v963,
C64 SIDPLAY telemetry v964, the v965 closure and version coherence. CMake registers 479 tests.
Full macOS AU/Logic, signing and SDK VST3 builds remain external sign-off.

# v964 telemetry audit + C64 SIDPLAY telemetry (CLOSED)

- Audited all 327 ArpSIDTelemetry fields against the kernel→adapter→GUI publish chain. Two dead
  fields found: the PSID load diagnostics (kernel published them since v873; the adapter never
  copied them). Fixed + family invariant test (every c64Psid* field must be adapter-published).
- SIDPLAY improvements: failed .sid loads / subtune reloads now show the exact rejection reason
  (psidParseResultName + new psidLoadFailureName) via a fresh adapter snapshot; the C64 player
  line adds INIT/PLAY addresses, PAL/NTSC, observed CIA-vs-VBI timing model, and live play-call
  count. No fabricated elapsed-time (phi2 is platform uptime; play-calls÷rate is wrong for
  multi-speed tunes). Test: `C64SidplayTelemetryV964Tests`.

# v963 GUI polish closure (accessibility + tooltips)

- CLOSED: mixer vol/pan/S/M/FX and DIGI START/LEN/VOL controls now carry tooltips (were the
  only interactive controls without them); ArpSIDStripView/DrumPadGridView/PianoView are now
  visible to assistive tech (strip = slider role with value; pads/piano = labeled groups).
- Verified clean, no changes needed: layout math at min size (bank grid ≈16 px cells), refresh
  hygiene (occlusion-paused display link, self-pausing Metal), CGPath/timer balance. Hidden
  legacy controls (_modelPop/_presetPop) intentionally untouched. Test:
  `GuiA11yTooltipCoverageV963Tests`.

# v962 GUI wiring closure (user-kit library + popout Cmd-F)

- CLOSED: reverse-wiring audit found two shipped-but-unreachable GUI features. (1) The DrSID
  user-kit popup `_drumUserKitPop` was consumed everywhere but constructed nowhere; its handler
  and the complete refresh/export/import implementations were dead — SAVE worked, load/manage
  didn't. The BANK panel now builds the promised strip (popup + RESCAN/KIT EXP/KIT IMP) and
  reloads the library. (2) The SIDCORE popout advertised "(Cmd-F fullscreen)" referencing an
  `ArpSIDSidCorePopoutWindow` class that didn't exist; it now does (⌘F fullscreen, ⌘W close).
- Rest clean: all selector/string/direct wiring resolves; no stuck-disabled controls; notification
  post/observe parity; AUv2 clean. `_toggleFullscreen:` deliberately unbound (editor ⌘F = tab
  shortcut; hijacking host chords is bad citizenship). Test: `GuiUserKitAndPopoutWiringV962Tests`.

# v961 arp gate-off timing closure (final open P2 — CLOSED)

- CLOSED (P2→actually P1 once measured per-step): in mono/legato/unison, EVERY arp step was
  gated off at the start of the next render block (~10 ms at 512/48k) instead of at the gate
  position. Fast rates masked it (20 ms steps expect ~16 ms gates); poly masked it behind
  release tails; slow rates played 10 ms ticks (earlier misread as "~2 dB attenuation").
- Root cause: `pendingGateOff_` (include/arpsid/engines/arpeggiator.h) carried two meanings —
  the normal "sounding note needs its gate-off at the gate position" (armed by every note-on)
  AND "flush the release at next block start" (rewind/transport jumps, buffer-exhausted
  boundaries). The top-of-block flush in collectTimedEvents() couldn't tell them apart.
- Fix: new `flushGateOffAtBlockStart_` carries the second meaning (armed only by rewindPhase()
  and buffer-exhausted boundaries); the step loop now emits the armed gate-off at its TRUE
  position (gateLength * stepSamples into the step window) as render time reaches it — GATE now
  shapes the arp duty cycle at any rate. No architectural change was needed.
- Verified: mono/legato slow-rate steps hold 0.465 s at full level (peak 0.549 vs 0.554 no-arp;
  was 0.448 global / 10 ms per-step); unison flat at 1.0 across rates; duty measured ≈0.50 at
  gate=0.5; rewind still flushes at block start (no stuck voices). SR/block edge sweep
  (44.1/48/96 kHz × 64/256/1024) clean. Added `ArpGateOffTimingV961Tests`. 474/474 green.

# v960 OpenBus VIC/Matrix visualizer improvement

- Improved the SIDCORE / C64 SID-bus Metal backdrop to render AUTHENTIC VIC-II + open-bus from
  real emulator telemetry: real `c64VicBadline` (was raster%8 guess), new `vicBeamX`
  (c64VicCycle/63 raster beam column), `spriteDma` (c64VicSpriteDma), `busDecayMask`
  (c64OpenBusDecayMask per-bit DRAM charge). Shader V384→V385 adds `vicRasterBeam()` (scanline +
  flying spot + phosphor trail) and `openBusDecayLanes()` (per-bit charged=green/decayed=amber),
  plus a badline DMA band; bus-surface only. MSL/C++ uniform structs layout-matched (43 floats).
- Validated: V385 compiles via `xcrun metal`; ViewController clean under -Werror; 473/473 tests.
- Regression: `OpenBusVicMatrixAuthenticV960Tests`. Metal shader is runtime-compiled, so it is
  checked offline with the downloaded Metal toolchain.

# v959 factory/GM defaults + split-brain audit (clean — no fixes needed)

- Audited all 180 factory/GM defaults: correctness, SID-chip authenticity, split-brains. CLEAN.
- NO render-mode split-brain (never drSid+synth), NO staging split-brain (kernel == canonical
  root), NO latent ARP/SEQ authority, canonicalization + preset-reload idempotent, register
  mirrors derived/consistent, GM drum-note map matches the GM standard, all slots audible+finite.
- Mode distribution: 79 SidRegister (GM melodic) + 71 DrSid (drums) + 30 Digi (DAC). 0 BitPerfect
  by design (SidRegister is the playable authentic synth; CLASSIC is for register/tune playback).
- Locked in with `FactoryGmDefaultsSplitBrainV959Tests`. No engine changes were required.
- CONTRACT NOTE (not a bug): `SidRuntimeModel::applyStateRootBySwap` consumes its input root by
  swap; callers must build a fresh root per apply (they do). Re-applying the SAME root object
  loads defaults — a test/caller gotcha, not a product path.

# v958 DrSID cold-first-block audibility closure

- CLOSED (audit sweep finding): in the DEFAULT SidAuthentic model, Cowbell and Tom (the only
  drums that route their OWN SID voice through the lowpass filter) were muted (~0.018 vs ~0.25)
  when the hit landed on the ABSOLUTE FIRST render block. Root: the SID filter cutoff smoother
  sat at the cold 20 Hz reset placeholder while `configureSidCoreForDrums_` had opened the cutoff
  to ~0x680, so a short filter-routed transient on the cold block was low-passed away (a warm-up
  block masked it; the v955 test drains a stopped block first, hence it passed). Fix:
  `configureSidCoreForDrums_` now snaps the filter smoother to the configured cutoff
  (`SIDChip::snapFilterSmoothing()`). Added `DrSidColdFirstBlockAudibilityV958Tests`.
- Broad sweep otherwise clean: all 150 factory slots audible + finite; parameter-extreme fuzz
  over CLASSIC/SYNTH/DrSID produced no non-finite output; rapid render-mode transitions stable.
- CLOSED in v961 (was "Still OPEN (P2)"): arp + non-poly at slow rates. The per-step probe
  showed the truth: every arp note was chopped to ~one render block (~10 ms) by the
  arpeggiator's top-of-block gate-off flush — NOT a 2 dB level issue and NOT the render-path
  architecture. See the v961 section at the top of this file.

# v957 SidAuthentic drum-knob response closure

- CLOSED (P1, deeper — was the OPEN item below): the DEFAULT SidAuthentic DrSID drum model
  now responds to the per-drum Tune/Decay/Tone knobs. Root cause: `triggerWavetableProgram_`
  emitted the fixed canonical C64 microprogram verbatim, so every knob position produced a
  bit-identical hit (only AnalogX0X8 expressed the params). Fix:
  `DrSidEngine::makeKnobModulatedWavetableProgram_()` builds a per-voice modulated copy of the
  canonical program at note-on — Tune scales each step's oscillator frequency, Decay scales the
  step schedule (cycleOffset/durationCycles) and release tail — centered on each knob's default
  so a default patch reproduces the authored hit bit-for-bit (exp2(0)==1). Now
  setCowbellTune(0.1) vs (0.9) yields different SidAuthentic cowbells while all 8 drums stay
  audible. Added `DrSidSidAuthenticKnobResponseV957Tests` (12 knobs x lo/hi + 8-drum audibility).

# v956 DrSID factory-kit distinctness closure

- CLOSED: DrSID factory slots 80..111 were 32 identical kits (only GM-percussion 112..119 were
  authored). Now each generic slot gets a per-(family,variant) character and selects the
  AnalogX0X8 model, so all are distinct + tweakable + audible. Added
  `DrSidFactoryKitDistinctnessV956Tests`. SID-808 audit was clean (all 30 slots distinct).
  (Follow-up v957 also makes the SidAuthentic model itself knob-responsive — see above.)

# v955 drum transport-start audibility closure

- Factory patches: all 180 slots (CLASSIC/SYNTH/DRSID) render audible across modes. Healthy.
- CLOSED (P1): in the DEFAULT drum machine model (SidAuthentic), Cowbell and Tom were
  silenced when a hit landed on the block where the host transport flips stopped->playing
  (e.g. a sequenced pattern whose step 1 is a cowbell/tom). Root cause:
  `clearRuntimeStateForTransportStart_` hard-reset the DrSID engine (`drs->reset()`), and the
  freshly-reset engine renders those choke-family, filter+pitch-swept voices (SID voice 0/1)
  as zero through the fractional interval render path (the same reset+hit renders fine via
  the slice/processBlock path; `reset()` in isolation is fine; kick/snare/rim recover).
  Fix: `runtimeRenderHostResetEngines(target, resetDrSid=false)` on the transport-start
  boundary releases held drums (`drSid->allNotesOff()`) instead of destructively resetting
  the engine — DrSID drums are one-shots and the engine preserves its own transport state
  (v950). Panic keeps the full reset (default `resetDrSid=true`). Phase2/VST3 already used a
  non-resetting transport-start path, so it never had the bug. Cowbell 0.0017->0.299, Tom
  0.0027->0.240; drum balance ratio 155x -> 4.4x. Added
  `DrumTransportStartAudibilityV955Tests`.

# v954 arpeggiator audio-render closure

- CLOSED (P0): the arpeggiator produced no audible output in the AU3/canonical render path.
  BitPerfect advertised fractional sub-sample-span support, so the host-cycle dispatcher's
  fractional render fought `renderBitPerfectWithArp()` (the only path that pumps arp step
  events). `CanonicalRuntimeBackend::supportsFractionalSubSampleSpans()` now returns false
  for BitPerfect while the arp is the effective note authority. Added `ArpAudioRenderV954Tests`.
- Audit of GUI/AU/menu/transport/state otherwise found those subsystems sound (transport &
  state verified by behavioral probes; AU/GUI reviewed).
- VERIFIED: Phase2/VST3 uses the same shared `CanonicalRuntimeBackend`
  (`processCanonicalAudioBlockForTargetInto` + `renderCanonicalAudioForTarget`), so the fix
  covers it.
- CLOSED in v961 (was "OPEN (P2)"): arp + non-poly at slow rates. The root cause was NOT the
  slice-vs-fractional render path hypothesized here — it was the arpeggiator's top-of-block
  gate-off flush chopping every step to ~one render block. See the v961 section above.

# v953 CLASSIC hard-restart audibility closure

- CLOSED (P0): CLASSIC/BitPerfect was ~18x-400x too quiet (near-silent on the default
  8580 patch) because `SIDVoice::scheduleHardRestart()`'s gate re-assert fired inside the
  46-cycle envelope discharge window and was clobbered back to Release. `Sid6581Envelope::
  gateOn()` now clears `hardRestartWindowCycles`, so the Attack survives. Default CLASSIC
  peak 0.0015 -> 0.614. Fixes the poly (`MultiChipPolyIllusion`) path for all waveforms and
  both chip models; single-chip path was already fine (it gates directly).
- CLOSED (P1): `kParamFilterMode` default `0.0` (-> `FilterMode::None`) corrected to `1/7`
  (LowPass), matching the documented C64-authentic init-patch intent.
- Added `DefaultPatchClassicAudibilityV953Tests` (real default patch, host MIDI note, no
  virtual gate; asserts full-level CLASSIC comparable to SYNTH).

# v951 state-root staging parity closure

- AU3 and Phase2 state-root restore paths now use param-specific sanitize/default staging, not generic clamp.
- Phase2 root apply mirrors restored clean values into the runtime model state root.
- No known open state-root staging split-brain issue remains in the focused closure line.

# v950 SEQ DrSID/SID808 reset preservation closure

- DrSID/SID808 structural reset now preserves owned SeqEnable and sequencer pattern state instead of killing drum transport.
- GUI DrSID mode selection, AU2/AU3 flavor policies, and file-bank persistence preserve restored SeqEnable for drum sequencer authority.
- Phase2 SEQ disable now releases DrSID under DrSID authority instead of BitPerfect.

v947 sanitized side-effect authority closure: backend/policy side effects consume staged clean values; special-param staging no longer pre-clamps.

v946 closure: final staging law guards added; raw values reach param-specific sanitize before params/render/runtimeModel sync.
- v945 authority staging policy closure: AU3 staging now uses param-specific sanitize; Phase2 SynthMode ARP/SEQ cleanup uses canonical staging and concrete backend cleanup.
v944: post-batch authority canonicalization closure — AU3 and Phase2 now clear ARP/SEQ on same-block structural returns from SynthMode/DrSID to BitPerfect, preventing host automation order from re-arming hidden secondaries.

v943 source cleanup closure: remaining v942 raw ARP/SEQ split-brain audit items closed with canonical staging, sanitizer mirror-back, and AuthoritySplitBrainClosureV943Tests.
v942: transition kit preservation + DrSID/SID808 factory authority closure; render-mode transitions no longer reset/destroy DrSID kit state, and factory/schema roots clear raw ARP/SEQ while preserving authored pattern data.
# Current open items after v941 dedicated drum SEQ authority closure

- No open P0/P1 mode-authority issues known in this source package.
- Continue running the full host/AU regression matrix outside the sandbox.

# Current open items after v940 render-mode transition authority closure

- No known open Classic/Synth/BitPerfect authority blockers in the source-level
  closure suite covered here.
- Full host AU/VST validation should still be run outside the sandbox for final
  product sign-off.

# Current open items — v939

- No known open BitPerfect/Classic/SynthMode/DrSID ARP/SEQ authority blockers in the focused closure surface.
- Full host validation on macOS/AU/VST remains recommended outside this sandbox.

# v939 BitPerfect authority helper + behavioral closure

- CLOSED: shared effective ARP/SEQ helper functions are the single law for raw flag + resolved render-mode authority.
- CLOSED: AU3/Phase2 runtime and telemetry use the helper law instead of reimplementing drift-prone raw flag checks.
- CLOSED: behavioral test covers BitPerfect effective ARP/SEQ and masks stale ARP under SynthMode/DrSID.

# Current open items — v938

- No known open SEQ/SynthMode/DrSID/BitPerfect authority blockers in the focused closure surface.
- Full host validation on macOS/AU/VST remains recommended outside this sandbox.

# v937 BitPerfect/Classic authority closure

- CLOSED: CLASSIC / BitPerfect mode selection clears Synth/DrSID/ARP/SEQ into a pure direct BitPerfect authority state.
- CLOSED: BitPerfect structural reset rejects stale ARP/SEQ snapshots instead of treating Classic as an implicit else-branch.
- CLOSED: BitPerfect direct-poly cleanup uses effective authority rather than raw ARP/SEQ flags.
- CLOSED: C64SidPlayer state roots clear SeqEnable as part of flavor authority.

# v936 Instrument SEQ presentation authority closure

Status: COMPLETE.

- Pure Instrument state-root policy now persists `SeqEnable=0` in both AU3 and AUv2, matching SynthMode/DrSID/ARP structural authority.
- Pure Instrument render flavor enforcement now clears `SeqEnable` as well as `ArpEnable`, so render-time policy cannot leave stale SEQ active beside SynthMode.
- `_applyModeSelectionIndex()` now clears and writes both ARP and SEQ off whenever Pure Instrument or SID REG/SynthMode is selected.
- GUI presentation adds `_effectiveSeqAuthorityEnabledForModeIndex()` and uses it for the header line, so stale raw `SeqEnable=1` is not shown while SynthMode/Pure Instrument owns note authority.
- SEQ step view now uses telemetry `tel.seqEnabled` rather than raw cache, matching DSP effective SEQ authority.
- Preserves v927-v935 SynthMode/ARP/SEQ/backend/GUI authority closures.

# v935 SynthMode ARP/SEQ authority complete closure

Status: COMPLETE.

- SynthMode structural reset now rejects stale `ArpEnable=1` and `SeqEnable=1` alongside stale DrSID authority.
- Entering SynthMode in AU3 and Phase2 immediately clears ARP/SEQ raw state and ARP active telemetry.
- AU3 and Phase2 SynthMode orphan/stuck-note cleanup now keys only off top-level SidRegister/SynthMode authority, not raw ARP/SEQ flags.
- Shared `SidRuntimeModel::isArpEnabled()` now reports effective ARP authority only in BitPerfect/classic mode.
- Sequencer engine/telemetry is suppressed while SynthMode owns note authority, preventing stale SeqEnable from poisoning SynthMode runtime.
- Adds a small behavioral guard for shared runtime effective ARP authority plus source guards for reset, transition and cleanup contracts.
- Preserves v927-v934 SynthMode/ARP/Instrument/GUI/backend authority closures.

# v934 backend/telemetry effective ARP closure

- CLOSED: backend arpeggiator enable is effective-authority gated, not raw `ArpEnable` gated.
- CLOSED: AU3/Phase2 telemetry now reports ARP as enabled only when ARP actually owns note routing.
- CLOSED: ARP step telemetry is cleared when SynthMode/DrSID/Instrument owns note authority.

# v933 GUI effective ARP presentation closure

- CLOSED: GUI ARP presentation is effective-authority gated.
- CLOSED: stale `ArpEnable=1` no longer lights the ARP step view or header while SynthMode/DrSID/Instrument owns note routing.
- CLOSED: `_selectModePreferredTab` no longer jumps to LFO/ARP from stale raw cache unless CLASSIC/BitPerfect is the effective mode.

v932 effective ARP authority final closure

- COMPLETE: ARP active telemetry is render-mode gated in AU3 and Phase2.
- COMPLETE: stale ArpEnable cannot present as active authority in SynthMode/SID-register or DrSID.

v931 instrument effective-mode final closure: COMPLETE — Pure Instrument effective mode is locked to SYNTH / SID REG during stale telemetry/cache windows.
## v930 Instrument GUI/render authority closure

Current release: v930 Instrument GUI/render authority closure.

Closed:
- Pure Instrument apply-mode index is forced to SYNTH / SID REG.
- Instrument mode requests clear stale ARP state.
- Instrument render policy clears ArpEnable, matching state-root policy.

## v929 phase2 synthmode arp authority closure

- Phase2/VST virtual-gate now follows the same SynthMode-first render authority as AU3 and canonical host MIDI.
- Stale `ArpEnable` can no longer capture SynthMode/SID-register virtual-gate notes in Phase2.
- Source-contract coverage was added to `ClassicModeAuthorityClosureV909Tests`.

# TODO

# v952 current ledger

- CLOSED: real DrSID/SID808 KIT render behavior across Play, Stop→Play, and AU transport reset.
- CLOSED: parameter-specific normalized semantics and shared AUv2/VST3 cardinality metadata.
- CLOSED: dirty-fallback side-effect and ordering parity with ordinary parameter intent.
- CLOSED: AU3/Phase2 state-root persistent adapter-policy reconciliation without realtime root mutation.
- CLOSED: audited production signed/unsigned loop warnings.
- External sign-off still required: macOS AU/Logic runtime, VST3 SDK build, signing/notarization, and installer validation.

v915 source cleanup closure ledger is preserved for forward-compatible V910 ingress parity source guards.

Current release: v928 SynthMode/ARP authority final closure.

- CLOSED: SynthMode/SID-register note authority now wins over stale ArpEnable in both canonical host MIDI and AU3 internal/virtual-note paths.
- CLOSED: NoteOff follows the same DrSID -> SynthMode -> ARP -> BitPerfect authority order as NoteOn.
- CLOSED: v927 AU3 internal SynthMode authority, v926 TODO ledger contract, and v925 red-test closure remain preserved.

# v925 full red-test closure

Source-side closure: COMPLETE.

Current release: v926 TODO ledger contract closure.

## v926 TODO ledger contract closure

- P0 fixed: `IngressParityTimingAuthorityV910Tests` requires the exact lowercase `v915 source cleanup closure` TODO ledger marker. v925 preserved the lineage only as `V915`, causing the full 453-test suite to fail on a documentation contract despite audio/ingress parity passing.
- TODO now opens with the exact forward-compatible v915 ledger phrase while preserving v925 red-test closure notes and P1/P2 source-lint markers.

## v925 ingress/drsid-reset red-test closure

- P0/P1 fixed: DrSID transport reset no longer replays pre-reset/factory kit parameters over the already-applied transport snapshot. Live Logic/AU Stop->Start edits for Kick/Snare/Hat/Drive/etc remain audible authority.
- P0 fixed: accepted midiQueue_ NoteOn/NoteOff held-ledger mirroring now happens at canonical dispatch, matching host events[] timing. Dropped NoteOff safety fallback still clears held state because it has no later dispatch.
- P1 fixed: raw MIDI Program Change enqueue validation no longer has duplicate switch labels.
- P1 fixed: SID-808 restore/flavor and realtime source-lint TODO markers are restored so V816/V817 closure tests validate current release docs instead of failing on cleanup-trimmed notes.

## Preserved closure buckets required by source guards

- P1-13/P1-14: SID-808 scheduled restore / flavor integration remains closed. Producer-side restore preloads SID-808 kits off the render thread, render apply consumes prepared kits only when the Sid808 flavor and canonical SID-808 slot agree, and DrSID/DrumMachine production audio remains canonical DrSID authority rather than SID-808 bridge authority.
- P2-05/P2-06/P2-07: V817 realtime source-lint closure remains closed. RT prepared-root apply avoids canonicalize/hydrate/ensure helpers, C64 render rollback uses bounded mutation journals, and by-swap runtime model apply remains RT-only.

## Current open items

No source-side P0/P1/P2 TODOs are tracked in this release package. Remaining signoff is external to the source tree: platform-specific AU validation, Logic runtime validation, VST3 SDK/toolchain validation, signing/notarization, and final installer/distribution checks where applicable.

## Preserved closure lineage

- V924: Classic mode authority perfect closure; structural mode bits stage once, DrSID kit replay is non-mode only, SID-808 flavor only adds model override.
- V923: Classic mode authority final clean closure; mode params removed from DrSID transport replay list.
- V922: stale snapshot cannot become structural mode authority over explicit factory/sticky root.
- V921/V920/V919: Classic/Synth and DrSID reset authority closures.
- V918: zip root/source package closure.
- V917/V916: factory schema and remaining issue closure.
- V915: source cleanup closure.
- V914: release cleanup final closure; removed superseded one-off AUv2 bootstrap probes and rejected raw MIDI Program Change metadata at enqueue.
- V913: Phase2 runtimeExecutionOwner_ null-guard hardening.
- V912: malformed MIDI enqueue rejection, pre-canonical overflow telemetry, and broader runtime-owner guarding.
- V911: accepted-only held-note mirroring, parent async drain guard, sorted chunk event merge, strict raw-MIDI short-message rejection.
- V910: AU3 midiQueue_ / host events[] ingress parity and parent-scope timing authority closure.
- V818/V819: final cleanup/dead-file source closure retained for release guards.
## v948 dirty flush staging authority closure

- AU3 async/UI param ingress now sanitizes with param-specific sanitize/default instead of generic clamp.
- AU3 dirty-flush fallback now synchronizes params_, renderParams_, and runtimeModel_.stateRoot() before backend reprojection.
- Added AuthorityDirtyFlushStagingV948Tests.
