# v970 test build-graph closure (handoff 22.7)

Status: build-infrastructure change complete; no production/runtime behavior changed, no shipped binary affected. Package identity: `0.0.690-pass380-v970-test-build-graph-closure`.

Compile the forensic patch bank once: source/forensic_patch_bank.cpp (a heavyweight TU that 14
separate test executables each recompiled) is now compiled once into a static library
(arpsid_forensic_patchbank) that those tests link. Cuts duplicate compilation and peak build memory
without weakening isolation; ODR-correct (no per-target compile defs), verified by a clean full
build with no duplicate-symbol errors. Production wrappers (VST3, AUv2 GUI smoke) unchanged — they
still compile their own copy (separate binaries). The AU-kernel header (~106 test includes) was
deliberately NOT extracted (production surgery, poor risk/reward here; remains open under 22.7).
Guard: BuildGraphForensicPatchbankLibV970Tests. Full CTest suite green.

# v969 test-suite integrity closure (CLOSED)

Status at v969: test-infrastructure audit-and-repair complete; no production/runtime behavior changed. Package identity was `0.0.690-pass380-v969-test-suite-integrity-closure`.

Audited all 491 test sources for orphaned/obsolete/flaky tests. Result: nothing genuinely
legacy/obsolete to remove — the apparent orphans test current behavior and are mostly registered
via string-constructing CMake foreach loops (dr808_${tgt}_tests, ${_test}_tests.cpp) invisible to
a filename grep; removing them would have destroyed real coverage (the repo already documents an
earlier over-eager cleanup that orphaned valid tests). Exactly one source was genuinely orphaned
(auv3_render_scratch_transport_v591) — now restored to the build with a linkage-tolerant
stable-scratch assertion. De-flaked ScopeTripleBufferMultiConsumerV687Tests: the producer ends the
run only after both consumers progress (bounded), removing a scheduling-starvation false failure
under loaded parallel CTest; coherency/monotonic invariants unchanged.

Validation: full CTest suite green; TestSuiteIntegrityV969Tests pins both repairs; version-coherence,
source-tree and audit-closure guards pass. macOS AU/Logic, signing and SDK VST3 remain external.

# v968 DrSID kit-save normalization + full-suite green closure (CLOSED)

Status at v968: focused source/runtime closure complete. Package identity was `0.0.690-pass380-v968-drsid-kit-save-and-queue-test-closure`.

Closed the remaining fixable DrSID/kit/GM audit item: DrSID user-kit SAVE serialized the live
kernel shadow verbatim, so a kit saved outside DrSID mode was stamped role=Drum yet loaded back
without entering DrSID mode. SAVE now routes through `_saveDrumKitDocumentToURL`, which forces
DrSidEnable=1/SynthMode=0 into the saved root for both `.arpsid` and `.json` (matching
import/export). Also repaired four pre-v965 tests that still asserted superseded contracts
(the focused suite was 476/480): PIPE-013 release-reserve queue admission and PIPE-001
arrival-order/emergency-kill sort (`ReleaseFinalClosureV404Tests`, `ReleaseRuntimeClosureV408Tests`),
published telemetry ARP/SEQ authority atomics (`ClassicModeAuthorityClosureV909Tests`), and the
multi-line DIGI render call (`GuiViewControllerWiringV590Tests`) — each re-pinned to current
behavior, not weakened.

Validation in this source pass: `DrSidUserKitSaveModeNormalizationV968Tests` plus the four
repaired tests and version coherence; AUv2 recompiles the edited ViewController cleanly. Full
macOS AU/Logic, signing and SDK VST3 builds remain external sign-off.

# v967 DrSID quick-kit base-profile parity closure (CLOSED)

Status at v967: focused source/runtime closure complete. Package identity was `0.0.690-pass380-v967-drsid-quick-kit-base-parity-closure`.

Closed: a split-brain between the on-screen DrSID quick-kit tone table and the canonical
factory DrSID base voicing. The "Standard" quick-kit row is authored to mirror the factory
base; the v909 factory tom-decay correction (0.30→0.44) was never mirrored into the GUI table,
so selecting "Standard" from the drum tab played a shorter tom than recalling the same default
kit from the host program list. The GUI base default and Standard row are re-synced to 0.44.
Also fixed: the "Standard" quick kit (GUI-only, slot -1) applied nothing to audio while
reporting a successful load — the deferred-apply guard returned early on negative slots;
GUI-only kits now apply their curated realtime profile (only the factory-patch load is skipped
when no slot backs the kit). The deliberately-distinct curated quick kits are unchanged.

Validation in this source pass: `DrSidQuickKitBaseProfileParityV967Tests` (pins the Standard-base
↔ factory-base tom-decay invariant and the slot-(-1) apply path) plus adjacent DrSID/factory
and version-coherence tests. Full macOS AU/Logic, signing and SDK VST3 builds remain external sign-off.

# v966 host parameter presentation authority (CLOSED)

Status at v966: focused source/runtime closure complete. Package identity was `0.0.690-pass380-v966-parameter-presentation-authority-closure`.

Closed: wrapper-local host display formulas that misrepresented the canonical DSP laws
(exponential LFO rate, limiter attack/release ms, quadratic portamento, sequencer tempo),
parameter-blind VST3 text parsing that clamped semantic values into [0,1], the VST3
UTF-8-byte-to-UTF-16 cast, unconditional VST editor stderr diagnostics, and the
`cent`/`ct` and `%`/`pct` unit-string drift. One shared parameter-ID-aware service
(`sid_parameter_presentation.h`) now owns format/parse/unit for AUv2, AUv3 and VST3,
delegating to the same law helpers the runtime renders with (now consolidated in
`math_utils.h` with inverses; numeric DSP behavior unchanged).

Validation in this source pass: `ParameterPresentationAuthorityV966Tests` (roundtrip across
every parameter, handoff §22.1 regression values, UTF-8 ⇄ UTF-16 correctness, wrapper
delegation source contracts) plus the adjacent v965 closure, LFO/limiter law and version
coherence tests. Full macOS AU/Logic, signing and SDK VST3 builds remain external sign-off.

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

# v964 telemetry audit + C64 SIDPLAY telemetry closure

Status: fixes complete; full local CTest suite 477/477 passed; AU reinstalled + cache cleared;
zip repackaged; auval SUCCEEDED.

TELEMETRY AUDIT: every ArpSIDTelemetry field (327) was cross-checked against the publish chain
(kernel atomics → ArpSIDDSPKernelAdapter readTelemetry → GUI). Result: exactly TWO fields were
declared and kernel-published but never copied by the adapter — `c64PsidLastParseResult` and
`c64PsidLastLoadFailure`, the PSID load diagnostics (v873 audit item 7 built the kernel side;
the adapter copy was never added). A failed .sid load could only say "load failed" while the
precise rejection reason sat unread in kernel telemetry. (A ~60-field "not consumed by GUI"
lead list was triaged: frame-ids/coalescing plumbing, host-app consumers, and diagnostics-only
counters — no other genuine dead fields.)

C64 SIDPLAY TELEMETRY IMPROVEMENTS:
1. The adapter now publishes both PSID diagnostic fields; a family invariant test requires
   every c64Psid* telemetry field to be adapter-published so this cannot silently recur.
2. `PsidLoadFailure` gained `psidLoadFailureName()` (mirrors psidParseResultName).
3. Failed .sid loads and failed subtune reloads now show the exact reason — e.g.
   "C64 SID PLAYER: load failed — parse BadMagic" or "— BootstrapRelocationFailed" — via a
   FRESH adapter snapshot (`_c64PsidLoadFailureDetail`; the load call is synchronous, and the
   polled _telemetry copy can be one vsync stale).
4. The C64 player info line now surfaces driver facts while a tune is loaded: INIT/PLAY
   addresses, PAL/NTSC clock, the OBSERVED timing model (CIA timer IRQ vs VBI raster, from
   psidCiaPlayAddressEntered/psidCiaIrqObserved), and the live play-routine call count
   (c64SidPlayCallCount, resets per tune load). Deliberately NOT shown: fabricated "elapsed
   time" (phi2 uptime is platform time, not tune time; play-call division is wrong for
   multi-speed tunes).
Regression test: `C64SidplayTelemetryV964Tests`.

# v963 GUI polish closure — accessibility + tooltip coverage

Status: improvements complete; full local CTest suite 476/476 passed; ViewController compiles
clean under -Wall -Wextra -Werror; AU reinstalled + cache cleared; zip repackaged; auval
SUCCEEDED.

Full-GUI audit across the dimensions not covered by earlier passes:
- LAYOUT ROBUSTNESS: verified panel math at the 720×520 minimum content size — the bank grid
  keeps positive cell heights (~16 px) after the v962 user-kit row; button rows wrap cleanly.
- TOOLTIP COVERAGE (fixed): the mixer strip's volume/pan sliders, one-letter S/M toggles, and
  five insert-FX popups had no tooltips (inconsistent — the neighbouring reverb-send had one),
  and the DIGI START/LEN/VOL sliders had none. All now carry per-channel/per-slot tooltips.
- ACCESSIBILITY (fixed): ArpSIDStripView (pitch/mod/breath strips), ArpSIDDrumPadGridView, and
  ArpSIDPianoView were invisible to assistive tech. Strips now expose a slider role with
  label+value (knobs already did); the pad grid and piano announce themselves with usage
  descriptions (velocity zones / octave shortcuts).
- REFRESH HYGIENE (verified clean): CVDisplayLink pauses on window occlusion
  (v246/v822 poll driver), Metal backdrop self-pauses when static; no always-on timers.
- MEMORY HYGIENE (verified clean): every CGPath create is paired with CGPathRelease; the one
  NSTimer is invalidated on teardown.
- False positives correctly left alone: _modelPop/_presetPop are hidden legacy controls
  (superseded by the native menu); _tabBar gets segment tooltips + accessibilityValue via
  ArpSIDConfigureVisibleTabBar_v268; _drumKitSeg via its chrome refresh.
Regression test: `GuiA11yTooltipCoverageV963Tests`.

# v962 GUI wiring closure — user-kit library strip + popout Cmd-F linked

Status: fixes complete; full local CTest suite 475/475 passed; ViewController compiles clean
under -Wall -Wextra -Werror; AU reinstalled + cache cleared; zip repackaged; auval SUCCEEDED.

A reverse-wiring audit (every defined action method vs every control that references it, across
selector, string-selector, and direct-call wiring) found two shipped-but-unreachable features:
1. DrSID USER-KIT LIBRARY: `_drumUserKitPop` was declared and fully consumed (reload/selection/
   enable logic) but NEVER CONSTRUCTED — the popup existed in no panel. Its change handler
   `_drumUserKitPopChg:` (loads the selected kit) and the complete async implementations of
   `_drumRefreshUserKitLibrary:` / `_drumExportUserKitBank:` / `_drumImportUserKitBank:` were
   dead code. Users could SAVE kits (DrSID footer) but never load or manage them — despite the
   DrSID panel label promising "switch to BANK for import/export and full library management".
   Fix: the BANK panel now builds the promised strip (DRSID USER KITS label + popup wired to
   `_drumUserKitPopChg:` + RESCAN / KIT EXP / KIT IMP buttons) and reloads the library on build.
   Note: NSPopUpButton IS an NSButton, so the popup carries tag=-1 to stay out of
   `_bankUpdateSlotHighlight`'s factory-grid recolor loop.
2. SIDCORE POPOUT Cmd-F: the popout window title advertises "(Cmd-F fullscreen)" and the code
   comment referenced an `ArpSIDSidCorePopoutWindow` key handler class that DID NOT EXIST — the
   popout was a plain NSWindow and ⌘F did nothing. The subclass now exists
   (performKeyEquivalent: ⌘F → toggleFullScreen, ⌘W → performClose) and the popout is created
   from it. The main editor's ⌘F (tab shortcut) is untouched; the popout is its own key window.
Rest of the audit was clean: all 162+ selector-wired actions resolve; string-wired
(NSSelectorFromString) actions verified; no stuck-disabled controls (every .enabled=NO ivar has
a re-enable path); notification post/observe parity balanced; AUv2 component has no orphan
actions. `_toggleFullscreen:` (main window) is intentionally left unbound: the editor's ⌘F is
already the tab shortcut, and hijacking host-window chords is bad plugin citizenship.
Regression test: `GuiUserKitAndPopoutWiringV962Tests`.

# v961 arp gate-off timing closure — the last tracked open issue is CLOSED

Status: fix complete; full local CTest suite 474/474 passed; AU reinstalled + cache cleared;
release zip repackaged; auval SUCCEEDED. No open P0/P1/P2 issues remain tracked in this package.

The former "P2: arp + non-poly slow-rate ~2 dB attenuation" was mis-diagnosed. Per-step
measurement showed every arp note was gated off at the START OF THE NEXT RENDER BLOCK (~10 ms
at 512/48k): mono/legato/unison at slow rates played 10 ms ticks instead of held steps. Fast
rates masked it (a 20 ms step expects a ~16 ms gate) and poly masked it behind release tails.
Root cause: `pendingGateOff_` in the Arpeggiator conflated "this sounding note will need its
gate-off at the gate position" (armed by EVERY note-on) with "flush the release at the next
block start" (transport rewinds/jumps, buffer-exhausted boundaries) — the top-of-block flush in
collectTimedEvents() fired for both. Fix: a dedicated `flushGateOffAtBlockStart_` flag carries
the flush meaning, and the step loop emits the armed gate-off at its TRUE gate position
(gateLength × stepSamples into the step window) as render time reaches it, so the GATE knob now
genuinely shapes the arp duty cycle at any rate. No render-path architectural change was needed
(the old TODO's slice-vs-fractional hypothesis was wrong).
Verified: mono slow-rate steps hold 0.465 s at full level (peak 0.549 ≈ no-arp 0.554; RMS 0.191
≈ held-note 0.193); unison flat 1.0 across rates; measured duty ≈ 0.50 at gate = 0.5; transport
rewind still releases the sounding note at the next block start (no stuck voices). Edge sweep
44.1/48/96 kHz × 64/256/1024 block sizes: clean for CLASSIC/SYNTH/DrSID/arp+mono.
Regression test: `ArpGateOffTimingV961Tests` (event-level duty + rewind flush + kernel render).

# v960 OpenBus VIC/Matrix visualizer improvement

Status: shader improved and validated; full local CTest suite 473/473 passed; the V385 Metal
shader compiles clean via `xcrun -sdk macosx metal`; ViewController recompiles clean under
`-Wall -Wextra -Werror`.

Made the SIDCORE / C64 SID-bus Metal backdrop (`ArpSIDSidCoreMetalBackdropView`) AUTHENTIC by
driving its VIC-II / open-bus visuals from REAL emulator telemetry instead of approximations:
- `badline` uniform now uses the real `c64VicBadline` flag (was a `raster%8==0` guess);
- new `vicBeamX` uniform = the real within-line VIC beam column (`c64VicCycle/63`);
- new `spriteDma` uniform = real `c64VicSpriteDma`;
- new `busDecayMask` uniform = real `c64OpenBusDecayMask` (per-bit open-bus DRAM charge).
Shader bumped V384→V385 with two new helpers — `vicRasterBeam()` (a live raster scanline +
flying-spot beam + phosphor persistence trail) and `openBusDecayLanes()` (8 bottom-strip lanes
showing each open-bus bit, glowing green while still driven and fading amber as it DRAM-leaks) —
plus an authentic badline DMA band, composited only on the C64 SID-bus surface (busMode > 0.5).
MSL and C++ uniform structs kept layout-matched (43 floats each). Regression test:
`OpenBusVicMatrixAuthenticV960Tests` (pins the real-telemetry wiring + struct-width parity).
NOTE: the Metal shader compiles at RUNTIME, so it is validated offline with the Metal toolchain
(`xcodebuild -downloadComponent MetalToolchain`; then `xcrun -sdk macosx metal -c`).

# v959 factory/GM defaults + split-brain audit (clean)

Status: audit complete, no defects found; full local CTest suite 472/472 passed.

Audited all 180 factory/GM defaults for correctness, SID-chip authenticity, and split-brains:
- Render-mode authority: NO split-brain — no slot sets both drSidEnable && synthModeEnable;
  canonicalization (sidCanonicalizeTopLevelRenderModeParams) resolves any priority accident
  (DrSid wins). Every slot's resolved mode is a real SID-chip mode.
- Distribution: 79 melodic GM programs → SidRegister (SYNTH, register-driven SID synth), 71 →
  DrSid (drum/percussion), 30 Digi (150..179, $D418 volume-DAC sample playback). 0 BitPerfect —
  by design: CLASSIC/BitPerfect is for exact SID-register/tune playback, SidRegister is the
  playable authentic synth used for GM instruments. All are SID-chip authentic.
- Staging parity: after applyStateRootCanonical the kernel's live mode flags match the
  canonicalized root exactly (no AU3 staging split-brain).
- Latent authority: NO dead raw ARP/SEQ — ArpEnable only set in BitPerfect, SeqEnable only in
  BitPerfect/DrSid.
- Idempotency: canonicalization is idempotent; reloading the same preset (fresh root per load,
  with churn between) yields an identical param image (no history-dependent state). NOTE:
  applyStateRootBySwap CONSUMES its input root by swap, so re-applying the SAME root OBJECT
  loads defaults — the product always builds a fresh root per apply, so this is contract, not a
  bug.
- SID register mirrors ($D4xx) are DERIVED from the high-level params (or forced-consistent in
  the authentic-snapshot path), so they cannot disagree with the authored patch.
- GM drum-note map (35..81) matches the GM percussion standard, mapped sensibly onto the 8 SID
  drum classes (kick/snare/hats/clap/cowbell/tom/rim).
- All 180 slots audible + finite through the SID chip (verified full sweep; regression covers a
  representative subset). Regression test: `FactoryGmDefaultsSplitBrainV959Tests`.

# v958 DrSID cold-first-block audibility closure

Status: fix complete; full local CTest suite 471/471 passed.

Follow-up audit sweep (finiteness + audibility across all 150 factory slots, a
parameter-extreme fuzz over CLASSIC/SYNTH/DrSID, and rapid render-mode
transitions) surfaced one residual: in the DEFAULT SidAuthentic model the Cowbell
and Tom voices — the only drums that route their OWN SID voice through the lowpass
filter (cowbell→voice0, tom→voice1) — were muted (~0.018 vs ~0.25) when the hit
landed on the ABSOLUTE FIRST render block. Root cause: after a DrSID reset the SID
filter's cutoff smoother sat at the cold 20 Hz reset placeholder while
`configureSidCoreForDrums_` had already opened the cutoff to ~0x680; a short
filter-routed transient on the cold block was low-passed away before the smoother
ramped up (an unrelated warm-up block masked it — which is why the v955 test, that
drains a stopped block first, passed). Fix: `configureSidCoreForDrums_` now snaps
the filter smoother onto the configured cutoff (`SIDChip::snapFilterSmoothing()` /
`SIDFilter::snapSmoothingToTarget()`), so the cold first block behaves identically
to a warmed one. Cowbell 0.018→0.299, Tom 0.018→0.240; kick/snare/hats unchanged.
Regression test: `DrSidColdFirstBlockAudibilityV958Tests`. The rest of the sweep
was clean (no silent slots, no non-finite output, stable mode transitions).

# v957 SidAuthentic drum-knob response closure

Status: fix complete; full local CTest suite 470/470 passed; AU installed + cache cleared;
release zip repackaged; `auval -v aumu ArpS ASID` SUCCEEDED.

The DEFAULT SidAuthentic DrSID drum model now responds to the per-drum Tune/Decay/Tone knobs.
Root cause: `triggerWavetableProgram_` emitted the fixed canonical C64 microprogram verbatim,
so every knob position produced a bit-identical hit (only the AnalogX0X8 overlay expressed the
params — see v956 below). Fix: `DrSidEngine::makeKnobModulatedWavetableProgram_()` builds a
per-voice modulated copy of the canonical program at note-on — Tune scales each step's
oscillator frequency, Decay scales the step schedule (cycleOffset/durationCycles) + release
tail — centered on each knob's default so a default patch reproduces the authored hit
bit-for-bit (exp2(0)==1). Verified: setCowbellTune/KickTune/TomTune/HatTune/SnareTone and every
per-drum Decay now reshape the SidAuthentic audio (diff > 1e-4) while all 8 drums stay audible.
Regression test: `DrSidSidAuthenticKnobResponseV957Tests`.

# v956 DrSID factory-kit distinctness closure

Status: source-side fix complete; full local CTest suite 469/469 passed.

DrSID kit audit (engine / kits / factory / mode), mirroring the SID-808 audit:
- SID-808 subsystem (previous pass): clean — engine, 5 kit families, all 30 factory slots
  (distinct), and the Sid808-flavor bridge routing (incl. transport start) all healthy.
- DrSID drums: all audible (v955 transport-start fix holds).
- DEFECT found: the 40 canonical DrSID factory slots (80..119) are 8 primary-drum families x
  5 variants, but only the GM-percussion slots (112..119) were authored — slots 80..111 all
  fell through to one generic default, producing 32 BIT-IDENTICAL kits (vs SID-808's 30
  distinct slots).
- Deeper root cause: in the DEFAULT SidAuthentic drum model, the per-drum Tune/Decay/Tone
  knobs are non-functional (bit-identical audio); SidAuthentic plays fixed canonical C64 drum
  microprograms and only AnalogX0X8 expresses those params. So the factory variations could
  not be heard under the default model.
- Fix: `applyFactoryDrSidNewKitCharacter_()` gives each generic slot (80..111) a deterministic
  per-(family,variant) character and selects the AnalogX0X8 model (where the knobs work), so
  each slot is a distinct, tweakable, all-audible kit (kick waveform diffs 0.027-0.034 across
  variants/families) instead of 32 aliases. Authored GM-percussion slots (112..119) are
  excluded. Added `DrSidFactoryKitDistinctnessV956Tests`.
- KNOWN (separate, deeper): SidAuthentic ignoring the drum knobs is a real usability gap for
  the default drum machine (not just the factory kits). Documented for a dedicated fix.

# v955 drum transport-start audibility closure

Status: source-side fix complete.

Drum-kit audit (all modes): all 180 factory patches render audible across CLASSIC/SYNTH/
DRSID. The default DrSID drum machine (SidAuthentic) had a real defect: Cowbell and Tom were
silenced whenever a hit landed on the block where the host transport flips stopped->playing
(a sequenced pattern whose first step is a cowbell/tom). Kick/Snare/Hats/Clap/Rim were fine.

Root cause: `clearRuntimeStateForTransportStart_` hard-reset the DrSID engine (`drs->reset()`)
on the Play edge; the just-reset engine renders the choke-family, filter+pitch-swept voices
(cowbell = SID voice 0, tom = voice 1) as zero through the fractional interval render path,
while the identical reset+hit renders fine via the slice/processBlock path. `reset()` in
isolation is fine, and kick/snare (microprogram) and rim (voice 2, no pitch sweep / choke
family) recover — it is specifically the reset + fractional-render interaction for those two
voices.

Fix: `runtimeRenderHostResetEngines(target, resetDrSid=false)` releases held drums
(`allNotesOff()`) instead of destructively resetting the DrSID engine at the transport-start
boundary. DrSID drums are one-shots and the engine already preserves its own transport state
(v950). Panic keeps the full reset. Phase2/VST3's transport-start path never reset DrSID, so
it was unaffected. Result: Cowbell 0.0017 -> 0.299, Tom 0.0027 -> 0.240, all 8 drums audible,
balance ratio 155x -> 4.4x. Added `DrumTransportStartAudibilityV955Tests`.

# v954 arpeggiator audio-render closure

Status: source-side fix complete.

- Fixed a P0 found during the GUI/AU/transport/state audit: the arpeggiator (the plugin's
  namesake feature) produced NO audible output in the AU3/canonical render path. The arp
  received held notes and even created BitPerfect voices, but its envelope stayed stuck at
  ~0.11 and the output was silent.
- Root cause: BitPerfect advertises fractional sub-sample-span support
  (`CanonicalRuntimeBackend::supportsFractionalSubSampleSpans()`), so the host-cycle
  dispatcher renders fractional intervals (`renderIntervalAccurate`) that fight
  `renderBitPerfectWithArp()` — the only render path that pumps the arp's timed step events
  into the engine and interleaves them with render sub-blocks (the sequencer is driven by an
  explicit per-block `advanceWindow`; the arp had no equivalent). The two paths raced and the
  arp voices were created but overwritten, leaving the arpeggio silent.
- Fix: `supportsFractionalSubSampleSpans()` now returns false for BitPerfect while the arp is
  the effective note authority, so the arp renders through its own interleaved slice path.
  Direct/non-arp BitPerfect notes are unaffected (still fractional). ARP now renders at full
  level across fast/mid/slow/tempo-synced rates (peak 0.61-1.0; envelope reaches 1.0).
- Added `ArpAudioRenderV954Tests` — the first test that renders arp audio through the kernel
  (the previous `arpeggiator_rate_exp` test only covered step-rate math).

Audit result for GUI / AU / menu / transport / state: transport (play/stop/release/live-
audition/seek/playStateKnown) and state (encode/decode round-trip: 0 param drift over 180
factory slots + live-mutation persistence) verified correct by behavioral probes. AU
parameter observer, fullState save/restore, and AUv2 render/allocation, plus the GUI popup/
menu handlers, reviewed and found sound (weak-ref guarded, bounds-checked, RT-safe). The one
material defect surfaced was the arpeggiator render bug above.

# v953 CLASSIC hard-restart audibility closure

Status: source-side fix complete; full local CTest suite 466/466 passed.

Fixed the "synth classic still doesn't work" P0: CLASSIC (BitPerfect) render mode produced
essentially no usable sound with the shipped default/init patch, while SYNTH/SID-REG always
played. Two independent defects, both fixed:

1. PRIMARY — hard-restart envelope clobber (`include/arpsid/core/sid_envelope_core.h`).
   The poly BitPerfect voice path (`MultiChipPolyIllusion`, the default topology) starts
   notes via `SIDVoice::scheduleHardRestart()`, which sets a 46-cycle envelope discharge
   window (`performHardRestart` -> `hardRestartWindowCycles=46`, forcing stage=Release/
   level=0 each tick) and a separate 46-cycle oscillator gate-reassert countdown
   (`kSidHardRestartCycles`). The countdown is serviced twice per SID cycle
   (`stepVoicesOneCycle_` + the `serviceCycleBoundary_` it calls), so the gate re-asserts
   (`gateOn()` -> Attack) well inside the discharge window and is immediately clobbered
   back to Release; after the window the voice is stuck in Release at zero -> silent note.
   `gateOn()` now clears `hardRestartWindowCycles`, so a rising gate ends the discharge and
   Attack survives. Every BitPerfect note (all waveforms, both chip models) was ~18x-400x
   too quiet; e.g. default CLASSIC host-note peak 0.0015 -> 0.614, on par with SYNTH (0.46).
   The single-chip 3-voice engine was unaffected because it gates directly (`setGate`), so
   the golden/fingerprint tests that drive `SIDChip` never exercised the bug.

2. SECONDARY — default `kParamFilterMode` was `0.0`, which the engine maps to
   `FilterMode::None` (index 0 of 8), not the documented LowPass (`// LowPass`, "C64
   authentic: LowPass 72%"). Default now selects LowPass (`1/7` == engine index 1), so the
   init patch has its intended filtered character rather than a bypassed filter.

The pre-existing v898 audibility test masked defect 1 because it set `kParamVirtualGate=1`
(fires a GUI virtual-keyboard note) and used loose thresholds. Added
`DefaultPatchClassicAudibilityV953Tests`: renders the real default patch with a host MIDI
note and NO virtual gate, asserting full-level CLASSIC output comparable to SYNTH.

# v952 runtime behavior/parity closure

Status: source-side implementation and local cross-platform validation complete.

- Added a real `ArpSIDDSPKernel` SID808/KIT render test. It proves step-zero audio after Play, Stop→Play, and `resetPreservingHostParameterSnapshot()`, and proves stopped follow-host transport neither auditions nor consumes the step.
- Fixed SID808 KIT edge state at transport, state-root, mode-transition, setup/teardown, and sequencer-disable boundaries.
- Replaced generic normalized clamping with a shared parameter-type contract for booleans, enums, 1..32 lengths, MIDI notes/controllers, factory slots, modulation sources, and SID bytes. Legacy floor-binned waveform/filter/octave values preserve their selected meaning.
- AUv2 and VST3 step metadata now use the same cardinality source. AU3 boolean units use the same type source. This removes the previous wrong 3-of-8 filter, 5-of-7 arp, 6-of-7 LFO, and 16-of-32 sequencer metadata.
- Queue-full dirty fallback now carries per-parameter generations, uses the ordinary execution-owner side-effect path, and rejects older queued values that would otherwise overwrite the fallback.
- Complete state-root installation reconciles persistent adapter-local policy in AU3 and Phase2 without re-canonicalizing or mutating the state root on the realtime thread.
- Mixed signed/unsigned production loops identified by the strict warning audit were made type-correct.
- C64 SID Player remains a component/player authority, not a synthetic fourth synth render mode. Existing `psidActive`/C64 telemetry and the unified audible-authority UI remain the presentation authority.

Validation: clean source build, local AUv2/AUv3/standalone bundle build, and full local CTest suite, 465/465 passed. Installed AU/Logic runtime, SDK VST3, signing release identity, and notarization remain external sign-off items.

Historical note: the v941 text below records an older BitPerfect-only SEQ policy. It was superseded by v949/v950, where DrSID/SID808 owns drum-pattern SEQ while SynthMode continues to block it.

# v951 state-root staging parity closure

- AU3 and Phase2 state-root restore paths now use param-specific sanitize/default staging, not generic clamp.
- Phase2 root apply mirrors restored clean values into the runtime model state root.
- No known open state-root staging split-brain issue remains in the focused closure line.

# v950 SEQ DrSID/SID808 reset preservation closure

- DrSID/SID808 structural reset now preserves owned SeqEnable and sequencer pattern state instead of killing drum transport.
- GUI DrSID mode selection, AU2/AU3 flavor policies, and file-bank persistence preserve restored SeqEnable for drum sequencer authority.
- Phase2 SEQ disable now releases DrSID under DrSID authority instead of BitPerfect.

v947 sanitized side-effect authority closure: backend/policy side effects consume staged clean values; special-param staging no longer pre-clamps.

v946: authority final staging closure — shared staging and target adapter no longer pre-clamp before param-specific sanitize; legacy AU3 stageRenderParam_ uses canonical staging.
- v945 authority staging policy closure: AU3 staging now uses param-specific sanitize; Phase2 SynthMode ARP/SEQ cleanup uses canonical staging and concrete backend cleanup.
v944: post-batch authority canonicalization closure — AU3 and Phase2 now clear ARP/SEQ on same-block structural returns from SynthMode/DrSID to BitPerfect, preventing host automation order from re-arming hidden secondaries.

v943: authority split-brain canonicalization closure — AU3 flavor enforcement, render-mode sanitizer, Phase2 direct staging, and ARP/SEQ backend policy now share one canonical authority state.
v942: transition kit preservation + DrSID/SID808 factory authority closure; render-mode transitions no longer reset/destroy DrSID kit state, and factory/schema roots clear raw ARP/SEQ while preserving authored pattern data.
# ArpSID v941 dedicated drum SEQ authority closure

- Dedicated drum/SID808 state-root policy now clears SEQ in AU3 and AUv2.
- Dedicated drum/SID808 AU3 render flavor enforcement now clears SEQ together
  with Synth/DrSID/ARP authority bits.
- File-bank DrSID/drum root canonicalization now clears stale ARP/SEQ when a
  root is promoted to DrSID authority.
- Direct host automation disabling SynthMode or DrSID now clears stale ARP/SEQ
  so CLASSIC / BitPerfect re-entry is pure unless ARP/SEQ is explicitly enabled
  afterwards.
- Added source-contract guards for the dedicated-drum SEQ closure and special
  parameter mode-disable authority edge.

# ArpSID v940 render-mode transition authority closure

- Render-mode transition is now an explicit performance-authority boundary.
- BitPerfect, ARP and Synth held voices are silenced when crossing modes.
- Canonical voice tokens and SEQ countdown/step state are cleared on transition.
- DrSID enable clears raw Synth/ARP/SEQ state through the shared parameter path.
- Backend, AU3 and Phase2 now share effective ARP/SEQ authority helpers for the
  remaining transition/projection paths.

# ArpSID v939 — BitPerfect Authority Helper + Behavioral Closure

Current package: `0.0.690-pass380-v939-bitperfect-authority-helper-behavioral-closure`.

v939 centralizes the first-class CLASSIC / BitPerfect ARP/SEQ authority law into shared helpers and adds a behavioral guard test. ARP and SEQ are effective only inside resolved BitPerfect mode; stale raw flags are masked under SynthMode/SID-register and DrSID across AU3, Phase2, telemetry and shared runtime model.

# ArpSID v938 — SEQ BitPerfect Authority Closure

Current package: `0.0.690-pass380-v938-seq-bitperfect-authority-closure`.

v938 closes the remaining SEQ effective-authority split: SEQ now runs and reports as active only under CLASSIC/BitPerfect effective authority. DrSID, dedicated drum flavors, SynthMode/SID-register, and Pure Instrument suppress stale SeqEnable consistently across AU3 runtime, AU3 telemetry, Phase2/VST runtime, Phase2 telemetry, and GUI presentation.

# v937 BitPerfect/Classic authority closure

Status: COMPLETE.

- CLASSIC / BitPerfect is now an explicit structural authority, not just a fallback when SynthMode and DrSID are off.
- GUI mode selection always clears stale `ArpEnable` and `SeqEnable` for CLASSIC, SYNTH/SID REG and DR SID authority switches.
- BitPerfect direct-poly stuck-note cleanup now gates on resolved render mode plus effective ARP/SEQ authority, not raw stale flags.
- Transport reset now has a BitPerfect structural authority block that rejects stale ARP/SEQ host snapshots and clears direct BitPerfect note state.
- C64SidPlayer AU3/AUv2 state-root policy persists `SeqEnable=0` with Synth/DrSID/ARP off.
- Preserves v927-v936 SynthMode/ARP/SEQ/Instrument/Phase2/backend/GUI closures.

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

Status: COMPLETE.

- Backend projection now disables the arpeggiator engine whenever SynthMode/SID-register or DrSID owns note authority, even if raw `ArpEnable=1` is stale in state/cache.
- AU3 telemetry publishes effective ARP authority and clears ARP step telemetry when ARP is not the active authority.
- Phase2 full telemetry publishes effective ARP authority instead of raw arpeggiator engine state.
- Preserves v927-v933 SynthMode/ARP/Instrument/GUI authority closures.

# v933 GUI effective ARP presentation closure

Status: COMPLETE.

- GUI presentation now uses effective ARP authority instead of raw stale `ArpEnable` cache.
- ARP step view, mode preferred tab selection and presentation header now suppress ARP whenever SynthMode/SID-register, DrSID, Pure Instrument or dedicated drum flavor owns note routing.
- Preserves v927-v932 SynthMode/ARP/Instrument authority closures.

# v932 effective ARP authority final closure

Status: COMPLETE.

- ARP authority is now effective-mode gated in AU3 runtimeIsArpEnabled().
- Phase2 runtimeModel arp-active telemetry is now gated by top-level render mode, matching v928/v929 note routing.
- SynthMode/SID-register and DrSID no longer expose stale ArpEnable as active note authority/telemetry.
- Preserves v927 AU3 internal SynthMode, v928 SynthMode-before-ARP, v929 Phase2, v930 Instrument GUI/render and v931 effective-mode closures.

v931 instrument effective-mode final closure: COMPLETE — Pure Instrument effective mode is locked to SYNTH / SID REG during stale telemetry/cache windows.
## v930 Instrument GUI/render authority closure

Current source closure: 0.0.690-pass380-v930-instrument-gui-render-authority-closure.

- Pure Instrument GUI is locked to SYNTH / SID REG for every mode request.
- Instrument GUI cannot map DrSID requests to CLASSIC/BitPerfect.
- Instrument GUI and render policy both clear stale ArpEnable.
- v927/v928/v929 SynthMode authority closures are preserved.

## v928 SynthMode/ARP authority final closure

- SynthMode/SID-register is now the primary note authority whenever enabled, even if ArpEnable is stale/on.
- Canonical host MIDI and AU3 internal/virtual-note paths now use the same authority order: DrSID, SynthMode, ARP, BitPerfect.
- NoteOff follows the same authority order as NoteOn.

# ArpSID Status

Current source closure: 0.0.690-pass380-v928-synthmode-arp-authority-final-closure.

Source-side closure: COMPLETE.

## v926 TODO ledger contract closure

- Fixed the final full-suite documentation/source-contract failure in `IngressParityTimingAuthorityV910Tests`: `TODO.md` now contains the exact lowercase `v915 source cleanup closure` ledger marker expected by the preserved v910 guard.
- No audio/render/MIDI timing logic changed in v926; this release only closes the TODO ledger contract mismatch from v925.
- v925 red-test closure is preserved: DrSID kit reset edits, Logic Stop/Start kit edits, state-apply policy, SID808 restore/flavor docs, realtime lint docs, and ingress held-mirror dispatch parity remain closed.

## Preserved closure lineage

- v925 red-test full closure.
- v924/v923/v922/v921/v920/v919 Classic/Synth/DrSID reset authority closures.
- v918 final source closure complete.
- v917/v916 factory schema and remaining issue closure.
- v915 source cleanup closure.
- v910 AU3 midiQueue_ / host events[] ingress parity and parent-scope timing authority closure.

# ArpSID v925 Status

Source-side closure: COMPLETE. V925 closes the red-test failures reported after v924: DrSID transport reset live-edit authority, accepted ring/events held-ledger release parity, SID-808/V817 TODO guard markers, and duplicate Program Change validation cleanup.

# v925 Red-Test Full Closure

- `FactoryDrsidKitBankSweepV238Tests`: DrSID kit edits survive transport reset because transport snapshot overlay is not clobbered by later factory/pre-reset replay.
- `LogicStopStartDrsidKitV238Tests`: Logic Stop/Start reset preserves DrSID/SID808 live kit edits while rejecting stale Program/BankSlot/VirtualGate/VirtualNote authority.
- `StateApplyReasonPolicyV240Tests`: HostTransportReset remains factory-root seed + live-audio overlay, not stale metadata authority.
- `Sid808RestoreFlavorIntegrationV816Tests`: P1-13/P1-14 closure markers are preserved in TODO and SID-808 restore/flavor source guards remain active.
- `RealtimeSourceLintV817Tests`: P2-05/P2-06/P2-07 closure markers are preserved in TODO and realtime source-lint guards remain active.
- `IngressParityTimingAuthorityV910Tests`: accepted ring NoteOn/NoteOff held mirroring is dispatch-time like host events[], closing release parity drift.



## v905 final timing/music verification closure

The v905 final timing/music verification closure remains preserved by the pass380 release-forward test lineage.

## v906 Phase2/VST3 timing/music parity closure

The v906 Phase2/VST3 timing/music parity closure remains preserved by the pass380 release-forward test lineage.

## v907 final timing/music sweep closure

The v907 final timing/music sweep closure remains preserved by the pass380 release-forward test lineage.

## v908 Phase2 no-output FX contract closure

The v908 Phase2 no-output FX contract closure remains preserved: no-output/scratch rendering continues to advance post-FX, Hifi, reverb and limiter state.

## v909 Classic-mode authority closure

The v909 Classic-mode authority closure remains preserved: Classic/Synth vs DrSID mode authority, Auto GM Drum Promotion opt-in policy, 8580 HMOS default, and no-output telemetry/FX continuity are still guarded by the release-forward closure tests.

## Preserved closure lineage

- v924: Classic mode authority perfect closure; structural mode bits stage once, DrSID kit replay is non-mode only, SID-808 flavor only adds model override.
- v923: Classic mode authority final clean closure; mode params removed from DrSID transport replay list.
- v922: stale snapshot cannot become structural mode authority over explicit factory/sticky root.
- v921/v920/v919: Classic/Synth and DrSID reset authority closures.
- v918: final source closure complete; zip payload root directory matches release identity and v916 source-tree name drift is closed.
- v917/v916: factory schema and remaining-issue closures.
- v915 source cleanup closure: stale disabled-gate comments removed and Source-side closure: COMPLETE marker preserved.
- v914: release cleanup final closure; Program Change metadata rejected at enqueue and superseded v827.py` through `v830.py bootstrap probes removed.
- v913: Phase2 runtimeExecutionOwner_ null-guard hardening.
- v912: malformed MIDI enqueue rejection, pre-canonical overflow telemetry, and broader runtime-owner guarding.
- v911: accepted-only held-note mirroring, parent async drain guard, sorted chunk event merge, strict raw-MIDI short-message rejection.
- v910: AU3 midiQueue_ / host events[] ingress parity and parent-scope timing authority closure.
- v904-v909: projection, Phase2 timing/music, no-output FX and classic-mode authority closures remain preserved.
- v818/v819: final cleanup/dead-file source closure retained for release guards.
## v929 phase2 synthmode arp authority closure

- Phase2/VST virtual-gate now follows the same SynthMode-first render authority as AU3 and canonical host MIDI.
- Stale `ArpEnable` can no longer capture SynthMode/SID-register virtual-gate notes in Phase2.
- Source-contract coverage was added to `ClassicModeAuthorityClosureV909Tests`.
## v948 dirty flush staging authority closure

- AU3 async/UI param ingress now sanitizes with param-specific sanitize/default instead of generic clamp.
- AU3 dirty-flush fallback now synchronizes params_, renderParams_, and runtimeModel_.stateRoot() before backend reprojection.
- Added AuthorityDirtyFlushStagingV948Tests.
