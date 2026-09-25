
## pass379 / fix-order #57 — SID-808 render-restore preload P0 closure

Fresh pass377 audit P0 #4 found that render-drained SID-808 restores queued the bridge factory-slot load but the queue was only guaranteed to drain during teardown/reset. Fixed by preloading matching SID-808 factory slots on the non-realtime `schedulePendingStateRestore()` producer side and making render-side apply skip the queue when the preloaded slot matches. A GUI/lifecycle fail-safe drain remains for unmatched/direct render applies. Guard: `Sid808RestorePreloadDrainV803Tests`.

## pass377 / fix-order #55 — final source closure validation

- Closed the final self-staling guard risk from pass376: `StaleVersionGuardSweepV800Tests` now derives the current package pass from `VERSION.txt` and rejects stale `passNNN` literals dynamically instead of hard-coding the previous release number.
- Added `FinalSourceClosureV801Tests` plus `RELEASE_FINAL_SOURCE_CLOSURE.md` as the top-level final source-release boundary.
- Source-level P0/P1/P2 closure is guarded; source-level P0/P1/P2 closure is guarded by `AbsoluteP0ClosureV797Tests`, `AbsoluteP1ClosureV798Tests`, `AbsoluteP2ClosureV799Tests`, and `FinalSourceClosureV801Tests`.
- External release validation is still intentionally separate: `auval`, Logic runtime, VST3 SDK/toolchain validation, and notarization remain pending until captured Mac logs prove them.

## pass376 / fix-order #54 — stale closure guard cleanup after absolute P2

**Scope.** Fully validated the user-supplied pass343 closure list against the current pass375 package and found remaining stale pass359 assertions in closure/status guard tests.

**Fix.** `Post18ClosureStatusConsistencyV766Tests`, `SourceOnlyClosureV767Tests`, `Auv2SourceHygieneV733Tests`, `MacosInstallStatusHandoffV770Tests`, `RootBuildScriptAuvalModeV771Tests`, `RootBuildScriptMacosClosureV772Tests`, `RootBuildScriptMacosClosureLoggingV773Tests`, `MacosBuildGreenStatusV774Tests`, and `RootBuildScriptClosureLogCommandV775Tests` now derive the current pass from `VERSION.txt` and verify `version.h`/README/status dynamically. Added `StaleVersionGuardSweepV800Tests`.

**Result.** The pass343 closure claims remain carried forward and are validated in the current package without stale pass-number failures. External AUv2/Logic/VST3/notarization validation remains intentionally separate.

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

## pass360 / fix-order #38 — CoreMIDI main-queue weak continuations

Standalone CoreMIDI refCons were already weak-boxed in v778, but `_handleMIDIPacketList:fromSource:` still queued main-thread blocks that captured `self` directly for MIDI activity and CC65/CC67 bridge state. Those blocks could retain the standalone delegate past teardown and were inconsistent with the zeroing weak callback policy.

Fix: the CoreMIDI handler now creates `__weak ArpSIDHostAppDelegate* weakSelf = self;` and every main-queue continuation strong-loads it before touching `_audioUnit`, `_softPedalSavedCutoff`, or `_flashMIDIActivity`. Direct real-time MIDI injection remains on the existing thread-safe inject path.

Guard: `StandaloneCoreMIDIMainQueueWeakContinuationsV784Tests`.


## pass359 / fix-order #37 — standalone preset panel snapshot hardening

- Fixed standalone host preset import/export panel completion handlers in `source/au3/ArpSIDHostAppDelegate.mm`.
- Export now snapshots `sp.URL` and the lowercase extension into immutable local values, weak-loads `ArpSIDHostAppDelegate`, and writes via the URL snapshot.
- Import now snapshots `op.URL`, reads from the snapshot, then weak-loads the delegate before publishing imported AU state.
- Added `StandalonePresetPanelSnapshotV783Tests` so the standalone host cannot regress to direct `self->_audioUnit` or direct `op.URL` / `sp.URL` use inside these completion blocks.


## Fix-order #36 — bank worker URL snapshot hardening (v782 / pass358)

Status: complete in pass358.

The pass357 bank worker refactor moved document IO to pure helpers, but several async bank/DrSID-kit worker blocks still captured `NSOpenPanel`/`NSSavePanel` and dereferenced `op.URL` / `sp.URL` inside background or delayed main-queue blocks. The selected URL and display filename are now copied in the panel completion handler before dispatch. Worker blocks use immutable `bankURL` / `importURL` / `exportURL` and copied display names only.

Guard: `BankWorkerURLSnapshotV782Tests`.
# ArpSID 0.0.686 → 0.0.687 — Audit Remediation

This document records the fixes applied in response to the realtime/lifetime/data-race
audit, and the few findings that are build/distribution-process items (not source bugs)
or deliberate accepted-by-design decisions.

---

## Second deep-audit pass (user 14-point list) — status

Validated this pass (build + auval 5/5 + full ctest 287/287; multiconsumer test
still TSan-clean):

* **(list #4) AtomicSnapshotSeqlock — lock-free asserts + no audio-thread stack
  scratch.** Added `static_assert(std::atomic<Word>::is_always_lock_free)` and the
  same for the generation atomic, so the producer can never take a lock on the
  audio thread. Rewrote `store()` (audio thread) and `load()` (GUI thread) to
  stream word-by-word with O(1) stack instead of a `Word[kWordCount]` temporary
  (multi-KB for a large scope Snapshot). (`scope_triple_buffer.h`)
* **(list #5) ScopeTripleBuffer no longer exposes destructive `tryConsume()`.**
  Moved the destructive single-consumer rotation to a private `tryConsumeSpsc_`;
  multi-consumer GUI code uses non-destructive `read()/peekLatest()`. The SPSC
  contract tests reach it through a `ScopeTripleBufferSpscTestAccess` friend.
* **(list #3) Drop/overflow diagnostics widened to 64-bit.** The per-block
  `SidTimedEventOverflowTelemetry` / `EventOverflowTelemetry` structs (all
  drop/replace counters) are now `uint64_t`, complementing the cumulative ingress
  atomics widened earlier. (`sid_event_queue.h`, `ArpSIDCanonicalEvents.h`, tests
  `release_final_closure_v404`, `release_runtime_closure_v408`.)
* **(list #6) AUv3 scratch pointer-stability — hard compile-time assert.** Made the
  frame-math helpers `constexpr` and added
  `static_assert(ArpSIDScratchStableRenderFrames() >= kArpSIDHardMaxFrames)`: the
  scratch reserved once at init provably covers the largest sanitized render
  request, which is the invariant the captured planar pointers rely on.
  (`ArpSIDAudioUnit.mm`)

Done this pass (blind GUI fixes — built + auval 5/5; USER must verify on-screen in Logic):

* **001↔patch flicker.** The patch readout asserted slot 0 ("001") before
  state-restore. AUv3: `_pinnedBankSlotNumber`/`_pinnedFactoryPresetNumber` now
  initialise to the `-1` "unknown" sentinel so the resolver falls through to the
  kernel state-root (actual loaded patch) instead of forcing slot 0
  (`ArpSIDAudioUnit.mm`). AUv2: the separate `pinnedBankSlotParamCache` mirror is
  now seeded from the audioUnit's resolved preset at instance creation instead of
  hardcoded slot 0 (`ArpSIDAUv2Component.mm`). This is NOT the strict
  flavor-filtering that caused the prior reset loop.
* **Legacy SID/patch menu.** The off-screen inline `_modelPop`/`_presetPop` are
  now explicitly `hidden=YES` (belt-and-suspenders so no stray layout path can
  resurface them). The native pull-down is the only chip/patch control.
* **Open-bus viz.** The bottom bit-row was a decorative hash; it now renders the
  ACTUAL open-bus byte bit-by-bit, plus a thin last-read-byte row, with lit-dot
  size modulated by SID traffic (`ArpSIDCreateSidCoreOpenBusPath`).
* **Render-mode oscillation (CLASSIC↔SID REG with no MIDI input).** The mode popup
  had TWO writers that resolved the mode from DIFFERENT sources: the telemetry
  refresh used the live render-thread telemetry (`tel->synthMode`, derived from the
  runtime resolver `sidResolveRenderModeFromLiveParams`), while
  `_syncPatchAndModePresentation` used the GUI's `_cachedParamValue` mirror. When
  the GUI cache lagged the AU after a patch load the two disagreed and overwrote
  each other every timer tick — the visible flicker (top popup CLASSIC, bottom
  status SID REG in the same frame). Fix: `_effectiveModeIndexFromTelemetry`'s
  param path now reads the AUTHORITATIVE bridge params (`[_au getParameterValue:…]`)
  instead of the GUI cache, and the telemetry-refresh popup writer now uses that
  same param path — so every mode reader (popup, status line, presentation)
  resolves identically and cannot fight. (`ArpSIDViewController.mm`)
* **Visualizers moved/animated with no audio data.** Several scope/meter views and
  the window-wide editor background advanced their sweep/beam phase EVERY vsync
  tick regardless of signal — the scope/meter views via an unconditional
  `_motionPhase += <const>`, and the main `ArpSIDEditorView` background aurora +
  raster beam via a wall-clock `phase` (only their OPACITY scaled with activity,
  not their MOTION). So the filter trace, meters and background drifted/swept even
  when silent. Fix: every idle-animation phase is now gated on real activity —
  scope/meter sweeps advance only with actual peak/RMS energy (and freeze once the
  envelope decays), and the background aurora/raster-beam freeze (beam hidden) when
  there is no meter/forensic activity and the host is not playing.
  (`ArpSIDViewController.mm`)
* **SID-core Metal 3D surface drifted continuously when idle (the real culprit).**
  The Metal shader animates off a `time` uniform fed from wall-clock `now` plus a
  per-vsync frame counter (`_sidCoreVsyncFrameCounter`), both advancing every frame
  regardless of SID/audio state — so the SIDCORE 3D surface drifted/swept with no
  notes. Fix: the shader animation now runs off ACTIVITY-GATED accumulators
  (`_sidCoreAnimTime` / `_sidCoreAnimFrame`) that only advance while the surface is
  actually doing something (audible peak, gated/enveloped voices, live C64 bus
  scope motion, or forensic mode). Every animation uniform (time, framePulse,
  latencyPulse, gpuLatencyFrame, cyclePhase, loaderBorderPhase) was repointed from
  the wall-clock/frame-counter source to these gated accumulators, so with no data
  the surface holds still and resumes smoothly when activity returns.
  (`ArpSIDViewController.mm`, `drawInMTKView:`)
* **C64 logo badge drifted continuously (the actual on-screen culprit, confirmed
  from a user screen recording).** The `ArpSIDC64LogoView` badge (the top-center
  "C64 / READY. SID·VIC" badge, used for both the header logo and the footer badge)
  animates a sweeping raster bar + waveform + colour rails from a `phase` that
  INCLUDED `_beat * 0.17` — the HOST TRANSPORT beat (`_c64BadgeView.beat = hostBeat`).
  With Logic's transport rolling, `hostBeat` advances every frame, so the bar swept
  smoothly forever with no audio (the "smooth continuous drift from the start").
  Fix: `phase` is now derived only from real audio level (`_activity`) and forensic
  mode — both zero when silent — so the badge holds still with no data, reacts only
  to actual sound, and settles smoothly as the level decays. `setBeat:` no longer
  forces a redraw (a rolling-but-silent transport can't trigger repaint).
  (`ArpSIDViewController.mm`, `ArpSIDC64LogoView`)
* **Green "REALTIME OPEN-BUS SCOPE" rolled with no notes (the SIDCORE-tab Metal
  surface).** The green `◈ SID BUS 3D / OPEN BUS / REALTIME OPEN-BUS SCOPE` panel
  is a Metal/GPU surface (`_c64BusMetalBackdropView`). It rolls because, while a
  C64 cockpit is viewed, the kernel advanced the cosmetic C64 telemetry-MIRROR CPU
  (`runRealtimeSidCoreCycles`) every block — so the emulated C64's idle KERNAL loop
  free-ran and cycled the open-bus scope with no audio. (Note: macOS screen
  recordings do NOT capture live Metal animation, so this drift looks frozen in a
  recording — it can only be seen live.) Fix: the mirror advance is now additionally
  gated on real audio activity (sounding voices or measurable output peak, read
  from the previous block), so the whole idle C64 cockpit — green open-bus scope,
  raster/CPU/memory readouts — holds still with no notes and comes alive only while
  audio is playing. The mirror is explicitly the cosmetic "not audible authority"
  surface, so audio is unaffected. (`ArpSIDDSPKernel.hpp`, `processBlock` C64
  telemetry-mirror gate)

* **(list #7) SID open-bus → dedicated `SidOpenBusApprox` blocker.** Added
  `C64PhysicalExactnessBlocker::SidOpenBusApprox` (bit 11), set in
  `physicalExactnessBlockers()` iff `sidOpenBusReadCount() > 0` (an OBSERVED SID
  open-bus read), distinct from the always-on capability blocker
  `OpenBusModelApprox`. GUI label `SID_OPEN_BUS_APPROX`. New behavioral test
  `c64_sid_open_bus_blocker_v689_tests` (288/288). (`c64_psid_runtime.h`,
  `ArpSIDViewController.mm`)

### Exactness #8–#13 + GUI pass 2 (v0.0.689)

* **(list #8) Color-RAM open-bus** — `C64Platform::colorRamHighNibbleOpenBusReadCount`
  counts CPU reads of $D800-$DBFF (high nibble = approximated open bus); the runtime
  sets the new `ColorRamOpenBusApprox` blocker when > 0. GUI label, test
  `c64_color_ram_open_bus_blocker_v690_tests`.
* **(list #9) No-sink SID reads** — `sidReadNoSink_` now increments
  `sidNoSinkOpenBusReadCount_`, folded into the runtime `sidOpenBusReadCount()`
  aggregate so no-sink reads set `SidOpenBusApprox` instead of being invisible. Test
  in v690.
* **(list #10) 0xFF SID-bridge fallback** — verified intentional: `0xFF` is the
  deliberate, **tested** open-bus model value (3 closure tests pin it), and those
  reads already increment `sidOpenBusReadCount` → flagged by #7's `SidOpenBusApprox`.
  No code change; observability was the real requirement and it is met.
* **(list #11) POTX/POTY** — `C64RuntimeSidSink::potxyReadCount` counts reads of
  $D419/$D41A (paddle A-D, un-modelled analog HW); new `PotXYApprox` blocker, GUI
  label, test `c64_potxy_blocker_v691_tests`.
* **(list #12) CIA/VIC granularity** — the coarse `Cia6526NotCycleExact` /
  `VicIINotCycleExact` are correct always-on CAPABILITY blockers; the finer OBSERVED
  reasons already exist in the RSID downgrade ledger (`VicBusStealApprox`,
  `CiaModelApprox`) and are GUI-exposed (`VIC_APPROX` / `CIA_APPROX`). With #13 the
  observed-risk surface now covers PSID too — granularity addressed via the ledger.
* **(list #13) PSID risk parity** — `totalPhysicalRiskBlockers()` now also sets the
  compact `ObservedDowngradeLedger` "not physically exact" bit for PSID when the run
  observed an approximation (open-bus SID/Color-RAM reads, POTX/POTY, or PSID CIA
  compat), matching RSID's ledger reporting.
* **GUI pass 2** — scope idle-shake deadzone (0.012; flat trace + frozen sweep when
  silent); open-bus GPU pure-black background + brighter bus elements. GM-drums-vs-
  DrSID mapping audited: complete, correct, bijective, well-tested — no fix needed.

### Concurrency #1/#2 + open-bus GPU (v0.0.690)

* **(list #1) Semantically-atomic ingress clear.** `BoundedMpscRing::drainAll()`
  raced open-endedly with producers, so a factory/root clear had an undefined
  boundary. Added `clearEnqueuedBeforeNow()`: it snapshots the enqueue barrier once
  and discards exactly the events producers had committed by the call — a clean
  before/after boundary (events pushed live *during* a patch-load reset survive,
  stale pre-reset events are dropped), never spinning on an in-flight producer.
  Both ingress ring `clear()`s use it. Behavioral test in
  `bounded_mpsc_ring_v688_tests`. (`arpsid_bounded_mpsc_ring.h`, `ArpSIDDSPKernel.hpp`)
* **(list #2) NoteOn/NoteOff loss under genuine fullness.** When the freelist +
  spill are both saturated, `captureIngressFallback_` used to drop NoteOn/NoteOff
  silently — a lost NoteOff leaves a STUCK note. Now a dropped NoteOff forces an
  all-notes-off stuck-note safety on its channel (global panic if channel
  unresolved) so nothing can hang, and a dropped NoteOn is counted. New per-type
  telemetry `ingressDroppedNoteOnCount()` / `ingressDroppedNoteOffCount()`. Test
  `ingress_noteoff_safety_v692_tests`. (`sid_runtime_model.h`)
* **Open-bus GPU true-black "godlike realistic" grade.** The bus surface read as a
  green wash because mesh/grid/tunnel/phosphor filled the whole frame after the
  black-bus mix. Those fills are now attenuated for the bus surface and a final
  black-point gamma + highlight lift + selective data bloom + filmic shoulder grade
  crushes the background to true black so only real bus data glows.
  (`ArpSIDViewController.mm`, `sidcore_frag`)

Still open:

* **Per-tab logo** — needs the user's on-screen view to know which tabs lack it.
* **(list #14) macOS AUv2/AUv3 build + auval** — done each pass (auval 5/5);
  codesign/notarize still requires an Apple Developer ID (process, not code).

All C/C++ headers and the kernel translation-unit changes were compile-checked
(`clang++ -std=c++17 -fsyntax-only`). The standalone unit tests that exercise the
changed core code were built and run green. The two `.mm` files (AUv3 AudioUnit and
the macOS ViewController output-tap) were changed following the existing atomic/
`__block`/lease patterns in those files but could not be fully compiled here without
the macOS AudioUnit/AppKit SDK + full Xcode project; they should be built on macOS.

## P0 — critical

1. **Cross-thread `LockFreeRing::clear()` on live ingress rings** —
   `ArpSIDMidiRingBuffer`/`ArpSIDParamIntentRingBuffer` now carry a per-ring epoch
   (`generation`). `clearQueuedFactoryPatchIngress_()` calls `invalidateLive()`
   (an atomic epoch bump) instead of `ring.clear()` (which mutated the consumer-owned
   `tail_` from another thread). Producers stamp the live epoch; the render drain
   loops skip events whose stamp != the current epoch. No live cross-thread `clear()`
   remains. (`source/arpsid_lock_free_ring.h`, `source/au3/ArpSIDDSPKernel.hpp`)

2. **PSID loader read render-owned `runtimeModel_` off the render thread** —
   added an atomic mirror `c64VideoStandardFallbackAtomic_`, published from the render
   thread (`updateTelemetry_`, reset path) and read by `loadPsidDataForRequest()`
   instead of `runtimeModel_.variantProfile()`. (`source/au3/ArpSIDDSPKernel.hpp`)

3. **`SidTimedEventQueue` allocated by default + by-value RT wrappers** —
   the default constructor is now NON-allocating; storage is acquired explicitly via
   the `AllocateStorage` tag ctor / `ensureStorage()`. The render-path by-value
   wrappers (`consumePendingEvents`, `processBoundCanonicalBlock`,
   `processCanonicalBlock`, the adapter `processBlock(int)`,
   `processCanonicalAudioBlockForTarget`, `consumeAndDispatchCanonicalPendingEvents`,
   `consumeDispatchAndRenderCanonicalBlock`) were removed; only the `*Into`/`*Direct`
   variants with caller-owned storage remain. Shipping owners (`pending_events_`,
   `canonicalQueueScratch_`) and tests now allocate explicitly. The RT-guard
   regression test was updated to the new contract.
   (`include/arpsid/core/sid_event_queue.h`, `sid_runtime_model.h`,
   `sid_runtime_execution.h`, `sid_runtime_target_adapter.h`, `ArpSIDCanonicalEvents.h`)

4. **`ScopeTripleBuffer` SPSC vs. multi-consumer GUI reads** —
   added a fence-based seqlock mirror. `read()`
   is a non-destructive coherent read safe for any number of consumers, and
   `peekLatest()` now routes through it (no longer mutates triple-buffer
   read/clean ownership). The SPSC `tryConsume()`/`publish()` path is unchanged.
   New test: `scope_triple_buffer_multiconsumer_v687_tests`.
   (`include/arpsid/core/scope_triple_buffer.h`)

   **Follow-up (deep-audit #4) — data-race-free under ThreadSanitizer:** the
   original mirror stored the snapshot as a *non-atomic* member guarded only by
   the generation counter. That is coherent in practice but a formal C++ data
   race (a non-atomic object read by consumers while the producer writes it),
   which TSan flags and the standard leaves undefined. The mirror is now a
   generic `detail::AtomicSnapshotSeqlock<Snapshot>` that backs the snapshot
   bytes with an array of `std::atomic` words, so *every* shared access (payload
   words and generation) is atomic — no non-atomic shared access, no data race
   in the C++ model, while the seqlock generation still discards torn reads.
   This brings the generic mirror in line with the already-clean
   `HostTransportSnapshotSeqlock` (which decomposes its POD into per-field
   atomics). The public API (`read`/`peekLatest`/`tryConsume`) is byte-for-byte
   unchanged, so no instantiation site changed. `scope_triple_buffer_multiconsumer_v687_tests`
   now passes under `-fsanitize=thread` with zero race reports.

5. **Kernel render API assumed `outputs[1]` exists** — `processBlock` now takes
   `outputChannelCount` and normalizes into a local 2-element array, making every
   `outputs[1]` access in-bounds; a missing right channel is `nullptr` and handled
   by the existing per-sample null checks. All callers (internal chunk recursion,
   `processBlockMono`, AUv2 adapter, AUv3 render block) pass the channel count.
   (`ArpSIDDSPKernel.hpp`, `ArpSIDDSPKernelAdapter.mm`, `ArpSIDAudioUnit.mm`)

### P0 — output-tap (macOS CoreAudio process-tap IOProc)

* **IOProc acquired the in-flight lease too late** — the lease
  (`_digiOutputTapCallbacksInFlight_v256_`) is now incremented at the very top of
  the IOProc, before any shared-state reads or the stopping/generation gates.
* **Start reset in-flight/scratch without proving drain** —
  `_digiStartOutputTapForPreview_v248_` now tears down and PROVES drain of any prior
  tap (fails closed if it cannot) before resetting counters / reassigning scratch.
* **Stop ignored drain failure** — `_digiStopOutputTap_v248_` now returns `BOOL`
  (drained), and the REC finalize path folds it into `recordCaptureSafe_v230` so a
  take is never committed while a tap callback may still be writing.
  (`source/au3/ArpSIDViewController.mm`)

## P1 — high

6. **`LockFreeRing` is a try-lock, not lock-free** — honest naming/comments;
   `tryPush()` returns `Ok/Full/Contended`; ingress wrappers keep split
   `droppedFull`/`droppedContention` counters with new accessors.
   New test: `lock_free_ring_contention_v687_tests`.
   *(Superseded in the v0.0.687 pass: ingress moved to `BoundedMpscRing` and the
   now-unused `LockFreeRing` + this test were removed — see deep-audit #10 below.)*

7. **State-restore mailbox ownership** — *superseded by the v0.0.690 P0-1 fix below.*
   The earlier claim that the version-tagged double-buffer was a correct
   single-writer/single-reader mailbox was wrong: its RT reader *swapped* the
   shared slots, so a writer that lapped a stalled reader (two publishes during a
   reader preemption between slot-select and swap) raced the reader's swap on the
   same `SidStateRootV1` — a torn-buffer/ABA data race that the post-swap
   `v1 != v0` recheck only detected *after* the racing access (UB).

   **v0.0.690 P0-1 fix** — replaced the seqlock-swap with
   `ArpSID::OwnershipMailbox<SidStateRootV1>` (`include/arpsid/core/sid_ownership_mailbox.h`):
   a three-buffer single-producer/single-consumer "latest-value-wins" mailbox that
   transfers buffer *ownership* through one atomic word. Producer and consumer
   never touch the same buffer — a lapping producer only ping-pongs between its own
   buffer and the parked one, never the buffer the render thread owns. The RT
   consumer (`tryConsume`) is wait-free, allocation-free, and performs no
   shared-buffer writes. (`source/au3/ArpSIDDSPKernel.hpp`)
   New behavioral test (three-buffer ownership invariant incl. writer-laps-reader,
   latest-value-wins, and a multi-threaded torn-buffer stress — TSan-clean):
   `state_root_mailbox_ownership_v748_tests`.

   **v0.0.690 P0-2 fix — serializable-template blob mailbox (same class of bug).**
   The render→UI state-template transfer was a version-counter two-slot seqlock: the
   render thread encoded a binary blob into a slot; `getState()`/UI decoded from the
   last completed slot and rejected torn results with a post-decode version recheck.
   The recheck stopped a torn result from being *accepted*, but the UI still
   *decoded from a slot the writer could concurrently overwrite* — with only two
   slots a writer that published twice during one slow decode wrote the very slot
   being decoded. That is a data race on the blob bytes (UB / TSan failure), and a
   torn length/count field could drive an out-of-bounds read or huge allocation
   mid-decode before the recheck rejected it. Replaced with
   `ArpSID::OwnershipMailbox<ArpSID::FixedBlobSlot<Cap>>`
   (`include/arpsid/core/sid_state_blob_slot.h`): the producer encodes into its own
   buffer and publishes ownership; the consumer owns the latest buffer for the whole
   decode, so the decode can neither race nor tear. The consumer-serializing mutex is
   retained; the producer never takes it and stays wait-free/alloc-free. Applied to
   BOTH wrappers — AUv3 `source/au3/ArpSIDDSPKernel.hpp` (producer = RT render thread)
   and VST3 `source/arpsid_processor_phase2.{h,cpp}` (producer = non-RT). New
   behavioral test (ownership invariant + fast-producer/slow-consumer torn-blob
   checksum stress — TSan-clean): `template_blob_mailbox_ownership_v749_tests`.
   *(Note: the VST3 edit could not be compiled in this environment — no VST3 SDK
   present — so it is a mechanical mirror of the compile-verified AUv3 change and
   needs a VST3-SDK build to confirm.)*

   **v0.0.690 P0-3 fix — render-thread state restore reached the allocating sanitizer.**
   `SidRuntimeModel::applyStateRootBySwap` is the audio-thread deferred-restore apply
   path, but it called `sanitizeStateRoot_` on the audio thread. That sanitizer
   allocates: it rebuilt the parameter/semantic vectors (via
   `sanitizePersistentStateRootForSerialization`) and copy-constructed the mod-route
   vector (`mod_routes = sanitizedModRoutes_(...)`) — heap activity in violation of the
   RT-safe / no-allocation contract. Fix: factored the full canonicalization into a
   free function `sidCanonicalizeStateRootForApply()`
   (`include/arpsid/core/sid_runtime_state_root_presentation.h`, with the mod routes
   now sanitized in place — no temporary-vector copy); the non-RT producer
   (`schedulePendingStateRestore`) runs it so the root is fully canonical before
   publish, and `applyStateRootBySwap` no longer sanitizes — its post-swap setup only
   reuses already-allocated capacity. `sanitizeStateRoot_` (the non-RT copy-apply path)
   now delegates to the same free function. New allocation-trap test proving the swap
   path makes zero heap allocations (also satisfies audit P2 "no render allocation-trap
   test"): `render_state_restore_alloc_trap_v750_tests`. (AUv3 only — the VST3 restore
   path uses the non-RT copy variant `applyStateRoot`, not the swap.)

   **v0.0.690 P0-4 fix — render-drained restore mutated DrumEngineHostBridge.**
   `applyStateRootCanonical` called `drumEngineBridge_.loadFactorySlot(slot)` for SID808
   factory slots. Once P0-1 made the deferred restore drain on the audio thread, that
   call ran on the render thread — violating the bridge's "render must only call
   processBlock(), never loadFactorySlot()/engine-config mutators" contract (a stale
   comment there even claimed the path was non-render). Fix: `applyStateRootCanonical`
   now takes `onRenderThread`; the render drain (`drainPendingStateRestore_`) passes
   `true` and QUEUES the slot via the bridge's RT-safe `queueSlotLoadNonRealtime()`
   (an atomic store), while the non-realtime `teardownReset()` drains it with
   `applyQueuedSlotNonRealtime()` (the actual load, off the audio thread). Non-RT
   callers (reset/setup/tests) pass `false` and load directly as before. Behavior note:
   a slot queued by a render-drained restore is applied at the next reset/setup
   (teardownReset) — frequent around state changes (Logic issues AudioUnitReset at
   stop/play). New test (queue/drain behavior + render-path guard proving the render
   branch queues and the non-RT teardown drains — audit P2 "render bridge-mutation
   guard test"): `render_drum_bridge_deferral_v751_tests`. (AUv3 only.)

   **v0.0.690 P0-5 fix — `realtimeEngineResetPreserveIngress()` name was a false promise.**
   The name claimed to preserve ingress, but it called `runtimeModel_.reset()` — a full
   model reset that clears the in-flight ingress (merge lanes + pending events) and
   wipes `state_root_`. That behavior is actually correct for the deferred state-apply
   it serves (`applyStateRootCanonical`): a full patch swap follows immediately
   (`applyStateRootBySwap` also clears ingress), so the new patch owns ingress and
   stale in-flight events from the old patch must not leak in. Held-NOTE continuity is
   preserved separately and explicitly by the caller, which snapshots the host-surface
   held-ingress identity lanes before the reset and re-arms them after via
   `sidReplayHeldNotes()`. Fix: renamed to `realtimeEngineHardResetForStateApply_()`
   and documented the real contract at both the definition and the call site — no
   behavior change, so it resolves the name/contract mismatch without risking the
   tuned state-apply flow. (AUv3 kernel; compile-validated by the AUv2 build.)

   **v0.0.690 P0-6 fix (fix-order #5) — `clearEnqueuedBeforeNow()` was not a true reset boundary.**
   The bounded Vyukov MPSC ring reserves a slot (CAS on `enqueuePos_`) and publishes
   it later (sequence store). `clearEnqueuedBeforeNow()` snapshotted the enqueue
   barrier and discarded committed items below it, but STOPPED at the first
   reserved-but-uncommitted slot — so a producer that reserved a slot *before* the
   reset and published it *after* leaked a stale pre-reset event across the boundary
   (it got popped against the new patch). Fix (`source/arpsid_bounded_mpsc_ring.h`):
   `clearEnqueuedBeforeNow()` now records the barrier in `resetBarrier_`, and `pop()`
   rejects (recycles + skips) any slot whose monotonic position is before
   `resetBarrier_`. The enqueue position itself is the epoch tag, so there is no
   producer-side epoch read to race the reset, and no false-discard of genuine
   post-reset events (those always reserve positions ≥ the barrier). Both real
   ingress rings (`midiQueue_`, `paramIntentQueue_`) go through this. New behavioral
   test using a white-box reserve/publish seam to deterministically reproduce the
   straggler: `ingress_reset_epoch_boundary_v752_tests`.

   **v0.0.690 P0-8 fix (fix-order #6) — unsafe C64 ROM helper indexing.**
   `C64RomSet::read{Basic,Kernal,Character}()` and the `poke*` setters indexed with a
   raw `address - base` subtraction. For any address below the device base the
   unsigned subtraction underflowed to a huge index — undefined behavior and a
   `-Warray-bounds`/UB-sanitizer hit (confirmed: UBSan reports an out-of-bounds
   reference for `readBasic(0x0000)`). Fix (`include/arpsid/core/c64_pla.h`): mask the
   offset to the array size — `(size_t(address) - base) & (size - 1)` — with
   `static_assert`s that the three sizes are powers of two and named base constants.
   Every uint16_t address now indexes within `[0, size)`. New behavioral test sweeps
   the entire address space through read/poke (ASAN-clean, no OOB) and checks boundary
   round-trips; it also builds warning-clean under `-O2 -Wall -Wextra -Warray-bounds
   -Werror`, closing audit P2 "warning-clean GCC Release gate for ROM helpers":
   `c64_rom_helper_range_safe_v753_tests`.

   **v0.0.690 P0-9 fix (fix-order #7) — AUv2 Cocoa view factory orphan after timeout.**
   `ArpSIDAUv2ViewFactory uiViewForAudioUnit:` dispatches the editor build to the main
   thread and waits with a 2s timeout. On timeout it returned nil, but the queued
   build still ran later — creating a controller/view, connecting it to the AU, and
   retaining it via associations the host never received: an orphan zombie editor.
   Fix (`source/au2/ArpSIDAUv2Component.mm`): a `stateLock` (`@synchronized`) makes the
   build commit and the timeout decision mutually exclusive via `callerAbandoned` /
   `builtOnMain` flags. If the build finishes after abandonment it disposes the editor
   it just built; if the build raced just ahead of the observed timeout, the timeout
   path disposes it on the main thread. New `disposeAbandonedAUv2Editor()` helper
   disposes the controller (`prepareForFinalEditorDisposal`) and clears the
   view/controller associations (idempotent, main-thread only). Result: never an
   orphan, never a dropped view that was actually built in time. Compile-validated by
   the AUv2 build; structural regression guard `auv2_cocoa_view_timeout_orphan_v754_tests`
   (a deterministic runtime test of the timeout race needs a blocked main thread + mock
   host, impractical here).

   **v0.0.690 P0-10 fix (fix-order #8) — AUv2 repeated open / attached-view replacement.**
   When the host requested a new Cocoa view while the previous editor view was STILL
   ATTACHED (`superview != nil`), the detached-reuse branch was skipped and the factory
   disposed the outgoing controller and built a replacement WITHOUT detaching the old
   view — leaving the host displaying a view backed by a torn-down controller
   (`prepareForFinalEditorDisposal` tears down runtime resources but never removes the
   view from its superview). Fix (`source/au2/ArpSIDAUv2Component.mm`): the replacement
   path now removes a still-attached outgoing view from its superview BEFORE disposing
   its controller, and clears the stale view/controller associations before building
   the replacement (so a failure mid-rebuild can't leave the AU pointing at a
   half-torn-down editor). Runtime coverage added to the AUv2 GUI smoke
   (`arpsid_auv2_gui_lifecycle_smoke.mm`: requests a new view while attached, asserts a
   fresh view is returned and the outgoing one was detached). Default-suite structural
   guard: `auv2_attached_view_replacement_v755_tests`. Compile-validated by the AUv2 build.

   **v0.0.690 P0-11 fix (fix-order #9) — CVDisplayLink callback used raw unretained self.**
   `CVDisplayLinkSetOutputCallback` received `(__bridge void*)self`; the vsync callback
   fires on a high-priority CoreVideo thread and cast it straight back to the
   controller. A callback in flight during teardown dereferenced freed memory (UAF).
   Fix (`source/au3/ArpSIDViewController.mm`): the callback context is now a heap
   `ArpSIDDisplayLinkContext` box holding a ZEROING `__weak` controller reference. The
   callback weak-loads the controller (ARC returns nil once the controller begins
   deallocating, so it can never touch freed memory). The box is owned via a
   `__bridge_retained` raw pointer (`_displayLinkCtx`) and transfer-released only after
   the link is stopped and `CVDisplayLinkRelease`d. Compile-validated by the AUv2 build;
   structural regression guard `cvdisplaylink_callback_lifetime_v756_tests`. (The two
   other `(__bridge void*)self` sites are DIGI AudioQueue capture contexts with a
   synchronous `stop(true)` teardown — separate DIGI items, untouched here.)

   **v0.0.690 P0-12 fix (fix-order #10) — parameter observer re-entered the AU bridge.**
   The AUParameter observer block (`tokenByAddingParameterObserver:`) re-read
   Program/BankSlot via `[au getParameterValue:pid]` from inside the callback. Observer
   blocks can fire on the render/automation thread, so re-entering the AU there risks
   re-entrancy/priority-inversion/blocking. Fix (`source/au3/ArpSIDViewController.mm`):
   the observer now just caches the observed value and defers; Program/BankSlot are
   already presentation params, so the deferred MAIN-thread sync
   (`_primeParameterCacheFromBridge`) re-reads them authoritatively through the bridge —
   the only safe place for a bridge read. Compile-validated by the AUv2 build; guard
   `param_observer_no_bridge_reentrancy_v757_tests`.

   **v0.0.690 P1-13 fix (fix-order #11) — no-sink POTX/POTY reads didn't set PotXYApprox.**
   A CPU read of POTX ($D419) / POTY ($D41A) with NO SID sink attached
   (`C64RomSet`/`C64Platform::sidReadNoSink_`) only bumped the generic
   `sidNoSinkOpenBusReadCount_`, so it never set the dedicated `PotXYApprox` physical-
   exactness blocker (only the SID-sink read path did). POTX/POTY depend on un-modelled
   paddle/pot analog hardware regardless of sink presence. Fix
   (`include/arpsid/core/c64_platform.h`): added `sidNoSinkPotxyReadCount_` (+ reset
   sites + accessor), bumped in `sidReadNoSink_` for $D419/$D41A. The `PotXYApprox`
   blocker and the PSID observed-risk parity in `c64_psid_runtime.h` now also consider
   `platform_.sidNoSinkPotxyReadCount()`. New behavioral test
   `c64_no_sink_potxy_blocker_v758_tests` (no-sink counter behavior + end-to-end runtime
   blocker via `attachSid(nullptr)`); updated the wiring assertion in
   `c64_potxy_blocker_v691_tests`.

8. **`fromCanonicalQueueInto()` diverged from the by-value converter** — it now
   sanitizes each event against `frameCount` and sorts, identical to the by-value
   version. (`source/au3/ArpSIDCanonicalEvents.h`)

9. **AUv3 render block captured a raw kernel pointer once** — added an AU-owned
   `std::atomic<ArpSIDDSPKernel*> _renderKernelPtr`, published by
   `allocateRenderResources` and cleared by `deallocateRenderResources`/`dealloc`;
   the render block loads it every invocation and fails closed when null.
   (`source/au3/ArpSIDAudioUnit.mm`)

10. **PSID retired-player single slot leaked on rapid handoff** — replaced with a
    bounded retired ring (`kRetiredPsidSlots = 8`); the render thread parks the
    outgoing player in a free slot and the non-RT collector frees all slots; a full
    ring is flagged (`retireOverflowCount_`) instead of silently leaking the common
    case. (`source/au3/ArpSIDDSPKernel.hpp`)

11. **RSID "strict" wording trap** — the `Clean` enum doc now states it requires
    BOTH a clean downgrade ledger AND zero physical-exactness blockers; the GUI badge
    presents the four claims as distinct facts (`STRICT PHI2`, `DOWNGRADES`,
    `PHYSICAL EXACT`) and never says "exact" unless truly exact; the physical blocker
    mask is now render-published (`c64RsidPhysicalBlockerMask_`), carried in the
    diagnostic snapshot (schema v5 → v6) and surfaced with a per-blocker reason
    string. (`include/arpsid/core/c64_psid_runtime.h`, `diagnostic_snapshot.h`,
    `ArpSIDDSPKernel.hpp`, `ArpSIDViewController.mm`)

### P1 — output-tap

* **REC record vector lacked a unified writer lease** — the output-tap record
  ingest now takes the SAME `_digiRecordTapCallbacksInFlight_v184_` lease as the
  AVAudioEngine tap, so a single drain wait proves no backend is mid-write.
* **C64 SID bridge timed-write array cleared twice per block** — removed the
  redundant manual pre-clear loop; `resetTimedWrites()` already zeroes the live
  range before resetting the count. (`ArpSIDDSPKernel.hpp`)
* **Output-tap scratch ring uses `std::vector` storage** — the
  reallocation-during-callback race is now eliminated by the P0 drain-before-reassign
  fix, and the scratch vectors are allocate-once (never resized after first start),
  so `.data()` pointers are pointer-stable. Converting to fixed `std::array` storage
  was judged a larger refactor with no remaining race and is deferred.

## P2 — medium

12. **Render-adjacent `std::sort`** — kept. The audit states `std::sort` is
    acceptable; the code carries `ARPSID_RT_SORT_CLASSIFICATION` markers proving
    allocation-free libc++ introsort with a total-order comparator, and a dedicated
    test pins the O(n log n)/capacity contract. A bucket-sort rewrite would risk
    event-ordering regressions for marginal benefit, so the algorithm is unchanged
    (accepted-by-design).

13. **Event-overflow telemetry was invisible** — canonical event-queue overflow
    (dropped NoteOn/NoteOff/automation/controller/transport, replaced-lower-priority,
    total) is now accumulated into render-published counters with public accessors,
    so dropped notes are observable in host tooling/validation.

14. **AUv3 stale captured render format** — the negotiated channel count is published
    to an atomic (`_renderOutputChannels`) and loaded per render invocation instead
    of snapshotted once. (`ArpSIDAudioUnit.mm`)

15. **Pure-SID capture lacked a control-side state machine** — added a CAS-based
    `PureSidCaptureState` (Idle/Recording/Busy); `startPureSid1Q1RecordCapture` and
    `copyAndStopPureSid1Q1RecordCapture` claim exclusive control access and fail
    closed on overlap, preventing two control-thread (UI timer/button) operations
    from interleaving.

16. **Non-render `std::this_thread::yield()` drain spins** — replaced with bounded
    `sleep_for` back-offs in the pure-SID and record-tap drain waits (non-render
    threads only). (`ArpSIDDSPKernel.hpp`, `ArpSIDViewController.mm`)

### P2 — build / distribution (NOT source bugs — require the macOS/Apple toolchain)

* **AUv3/Logic PlugInKit registration not asserted** — this is a runtime/packaging
  validation step (`pluginkit`/`auval` on an installed bundle), not a source change.
  It must be asserted on macOS after install; it cannot be exercised in this
  source-only environment. (`BUILD-VALIDATION.txt` already records the override.)
* **Products ad-hoc signed, not notarized** — notarization requires an Apple
  Developer ID identity and Apple's notary service (`codesign` with a Developer ID
  cert + `notarytool submit`/`stapler`). This is a release/distribution process step,
  not a code fix.
* **Tests are source-pattern-heavy** — `scope_triple_buffer_multiconsumer_v687_tests`
  exercises a real 1-producer/2-consumer race rather than a source grep, and now
  passes clean under `clang -fsanitize=thread` (see the deep-audit #4 section).
  Full TSAN coverage of the AU kernel render path requires building the AU target
  under macOS and is recommended as a follow-up.

---

## Deep-audit remaining items — v0.0.687 cleanup pass

This pass closes the lower-risk remaining items from the external deep-audit
(#4 was the concurrency item, fixed earlier in this doc).

5.  **`ScopeTripleBuffer::reset()` was a public quiescent-only method** — renamed to
    `resetQuiescent()` so the "not safe while producer/consumer is live" precondition
    is explicit at every call site. No external callers existed.
    (`include/arpsid/core/scope_triple_buffer.h`)

7.  **`SidTimedEventQueue::ensureStorage()` could allocate** — renamed to
    `ensureStorageNonRealtimeOnly()` so the may-allocate / off-render-thread contract
    is impossible to miss. Internal-only; no external callers.
    (`include/arpsid/core/sid_event_queue.h`)

10. **Dead `LockFreeRing` removed** — ingress moved to the correct `BoundedMpscRing`
    in an earlier pass, leaving `LockFreeRing` referenced only by `#include` (never
    instantiated) plus its own test. Deleted `source/arpsid_lock_free_ring.h`, the
    two dead `#include`s (`arpsid_processor_phase2.h`, `ArpSIDDSPKernel.hpp`), the
    `lock_free_ring_contention_v687_tests` source + CMake target. `BoundedMpscRing`
    (and `bounded_mpsc_ring_v688_tests`) is the current ingress-ring + coverage.

12. **Drop/overflow counters widened uint32→uint64** — the cumulative ingress drop /
    merge-overflow / dispatch-drop atomics (and their accessors) are now 64-bit so a
    long session cannot wrap them: `ingress_dropped_`, `direct_dispatch_dropped_`,
    `mergeOverflowTelemetry_` + `ingressDroppedCount()/consumeDirectDispatchDropped()/
    mergeOverflowCount()` (`sid_runtime_model.h`), `SidIngressLane::overflow_` +
    accessors (`sid_ingress_lane.h`), and the `sidIngressMerge` telemetry parameter
    (`sid_ingress_merge.h`). The per-block `…OverflowTelemetry` structs in
    `sid_event_queue.h` / `ArpSIDCanonicalEvents.h` are intentionally left `uint32_t`:
    they reset every block/consume and cannot wrap within a cycle, and the only
    genuinely cumulative session counter (`canonicalQueueOverflowCount_`) was already
    `uint64_t`. This avoided churning ~6 same-type (`checkEq<T>`) telemetry tests.

15. **AUv3 render reentrancy guard** — added a defensive `std::atomic<bool>`
    (`_renderReentryGuard`) that the `internalRenderBlock` CAS-claims at entry and
    releases via RAII on every exit path. AU render is contractually non-reentrant,
    so this never trips in practice; if a host ever violated it, the second
    invocation now renders silence instead of corrupting the shared scratch/event
    buffers. (`source/au3/ArpSIDAudioUnit.mm`)

16. **AUv3 parameter observer null-guard + dropped-write telemetry** — the
    `implementorValueObserver`/`implementorValueProvider` now explicitly null-guard
    `_adapter` (sending to a nil adapter during the teardown window is a silent ObjC
    no-op that would hide lost writes). Writes lost to a nil adapter are counted in
    `_droppedParameterWrites`; reads fall back to the pinned preset value (Program/
    BankSlot) or the parameter default. (`source/au3/ArpSIDAudioUnit.mm`)

20. **Exactness-wording policy verified** — the gating predicate already matches the
    policy: `rsidPhysicallyExact() == rsidStrictPhi2PathActive() && downgradeLedger==None
    && physicalExactnessBlockerMask()==0` (`c64_psid_runtime.h`). Code/doc comments are
    consistently hedged ("non-cycle-exact", "not value/cycle-exact", "PHI2 discipline ≠
    physically exact"). Tightened one loose comment in `c64_embedded_roms.h`: embedding
    the ROMs clears only the two ROM-related blockers and is NOT, on its own, a
    "physically exact" claim.

### Verified / accepted-by-design this pass

14. **Scratch resize-while-live** — every `_renderPlanar*` resize is wrapped in the
    `_renderScratchEpoch` odd→even protocol and occurs only at lifecycle transitions
    (init, `allocateRenderResources`, `setMaximumFramesToRender` while render is not
    allocated) — i.e. when the AU contract guarantees no live render block. The render
    block's entry/exit epoch-stability check remains the fail-closed backstop. No
    resize-while-live path exists; no code change needed.

17/18. **Parameter-automation sample-accuracy telemetry** — completed in
    fix-order #17 and #18. The kernel now separates timed queue-drop from
    dirty-flush fallback, and AUv2 ramp expansion publishes generated-vs-dropped
    anchor telemetry. Runtime AUv2/Logic validation is still a macOS release step,
    but the source-level telemetry closure is no longer deferred.

## Fix-order #12 — VST Cocoa factory preset range (audit P1#20)

**Bug.** `source/gui/arpsid_vst_cocoa_bridge.mm` built `_factoryPresets` with
`for (int i = 0; i < 128; ++i)`, so the VST Cocoa factory preset list exposed only
the legacy 128 slots while the canonical factory bank/range is 180
(`kCanonicalFactoryPatchSlotCount`, `kCanonicalFactoryPatchSlotMax`).

**Fix.** The VST Cocoa bridge now allocates the list with
`arrayWithCapacity:(NSUInteger)ArpSID::kCanonicalFactoryPatchSlotCount` and iterates
`i < ArpSID::kCanonicalFactoryPatchSlotCount`, while preserving canonical slot names
via `ArpSID::factoryPatchNameForSlot(i)`.

**Validation.** Added default-suite source guard
`vst_cocoa_factory_preset_range_v759_tests`, which verifies the canonical count is
used and rejects the old hard-coded `128` loop. Targeted ctest passed. The VST target
remains SDK-gated in this environment, so no VST3 binary build was performed here.


## Fix-order #13 — DrSID legacy/canonical context policy cleanup

**Bug.** `include/arpsid/core/drum_context.h` still documented the legacy 0..127
DrSID projection classifier as if it stayed in lock-step with
`isAuthoredDrSidProjectionFactorySlot()`. That was stale after the canonical 180-slot
factory layout: authored DrSID now covers 80..119, SID-808 owns 120..149, Digi owns
150..179, while legacy saved-bank compatibility remains the old projection set
{47, 112..124, 127}. The stale comment could lead future cleanup to collapse two
separate policies and misclassify restored legacy banks or canonical drum pages.

**Fix.** The drum-context header now states the split explicitly: canonical callers use
`factorySlotContext()` / `isDrSidFactorySlot()`, while old saved-bank compatibility uses
`factorySlotContextLegacy()` / `isLegacyDrSidProjectionSlot()`. Also removed a stale
`(128)` factory-bank assertion message in `forensic_engine_sanity_v527_tests.cpp`.

**Validation.** Added default-suite guard
`drum_context_legacy_canonical_split_v760_tests`, which pins the canonical ranges
(DrSID 80..119, SID-808 120..149, Digi 150..179), the legacy compatibility set
(112..124 plus 47/127), and rejects the old lock-step wording. Targeted tests passed.

## Fix-order #14 — SID-core single-authority mutable surface cleanup

**Bug.** `SidRuntimeModel` still exposed generic mutable aggregate accessors
`dynamicState()` and `pendingEvents()`. Even though most production paths had moved to
canonical typed mutation APIs, those compatibility-shaped names made it too easy for
wrappers, legacy processors, or future cleanup to bypass the single-authority mutation
law and mutate SID-core state/queues directly.

**Fix.** Removed the generic mutable spellings from the public surface. Mutable dynamic
state access now requires the explicit opt-in name
`dynamicStateInternalForCanonicalRuntimeOnly()`, and mutable queue access requires
`pendingEventsNonRealtimeOnly()`. Existing internal/legacy call sites were updated to use
those long names. The const diagnostic views remain available as `dynamicState() const`
and `pendingEvents() const`.

**Validation.** Added default-suite guard `sidcore_single_authority_surface_v761_tests`,
which rejects the old generic mutable accessors, verifies the explicit internal/non-RT
names, checks the single-authority documentation, and pins the updated call sites.
Targeted tests passed in this sandbox.


## Fix-order #15 — wrapper-owned state apply policy cleanup

Moved the state-apply authority mapping out of the AU wrapper and into canonical core policy:

- Added `include/arpsid/core/sid_runtime_state_apply_policy.h` with `SidStateApplyReason`, `SidStateOverlayPolicy`, and `sidStateOverlayPolicyForApplyReason(...)`.
- Updated `source/au3/ArpSIDDSPKernel.hpp` to include/call the core mapper instead of owning `StateApplyReason`, `StateOverlayPolicy`, and `policyForStateApplyReason(...)` definitions.
- Updated `source/arpsid_processor_phase2.h` to include the same canonical policy header, keeping AU/VST on one shared law.
- Updated `source/tests/state_apply_reason_policy_v240_tests.cpp` to assert the core policy directly.
- Added `source/tests/wrapper_state_apply_policy_core_v762_tests.cpp` to guard against reintroducing wrapper-owned policy tables.

Validation: `StateApplyReasonPolicyV240Tests` and `WrapperStateApplyPolicyCoreV762Tests` pass, plus version/source hygiene guards.

## Fix-order #16 — DIGI atomic protocol surface cleanup

**Bug.** The adapter implementation already treated DIGI model + sample-bank state as
one matched persistence unit, and the old split selectors were fail-closed compatibility
stubs. However, `ArpSIDDebugAdapterLike` in `source/au3/ArpSIDViewController.mm` still
advertised those legacy split selectors as optional capability. That left a future
GUI/AU call site one `respondsToSelector:@selector(getDigiModel:)` away from
reintroducing model/sample-bank split-brain across an atomic update boundary.

**Fix.** Removed the legacy split DIGI selectors from the public GUI/debug protocol.
The public capability surface now exposes only `getDigiModel:sampleBank:` and
`setDigiModel:sampleBank:` for matched snapshots/publication. The adapter category in
`ArpSIDDSPKernelAdapter.h/.mm` still keeps the old split selectors as private
source/binary compatibility stubs that fail closed: default empty reads and no-op
writes.

**Validation.** Added default-suite guard `digi_split_protocol_surface_v763_tests`,
which rejects split DIGI selectors inside `ArpSIDDebugAdapterLike`, requires the atomic
matched selectors, and verifies the adapter still contains fail-closed compatibility
stubs. Targeted tests passed.



## Fix-order #17 — Parameter dirty-flush fallback telemetry

**Bug.** Parameter automation ingress diagnostics conflated two different events: a timed
`paramIntentQueue_` push failure means the sample-accurate/ramped intent was dropped,
but `enqueueParameterIntent()` already marks the parameter dirty before the push. The
render thread therefore still applies the latest value through `flushDirtyParams_()` at
the next block boundary. Reporting only the generic ingress drop made diagnostics look
like value loss when it was actually timing-resolution loss plus dirty-flush fallback.

**Fix.** `source/au3/ArpSIDDSPKernel.hpp` now owns a dedicated
`paramIntentDirtyFlushFallbackTelemetry_` counter and accessor
`paramIntentDirtyFlushFallbackCount()`. The queue-full branch still increments
`ingressDropTelemetry_`, but also records the dirty-flush fallback, documenting that
the latest normalized value remains preserved even though sample-accurate timing was
lost. The reset path clears the new counter with the other ingress telemetry.

**Validation.** Added default-suite guard
`parameter_dirty_flush_fallback_telemetry_v764_tests`, which pins the dirty-before-push
ordering, the queue-drop telemetry branch, the dedicated fallback counter/accessor, and
the reset path. Targeted tests passed in this sandbox.


## Fix-order #18 — AUv2 ramp-anchor/drop telemetry

**Bug.** AUv2 `kParameterEvent_Ramped` expands a host ramp into up to 64 timed
parameter anchors, but the wrapper previously ignored whether each timed
`enqueueParameterIntent(...)` actually entered the kernel queue. Kernel-side pass339
telemetry already distinguishes generic ingress-drop from dirty-flush value
preservation, but AUv2 still had no wrapper-level visibility into how many ramp
anchors were generated versus how many lost sample-accurate timing.

**Fix.** `ArpSIDDSPKernel::enqueueParameterIntent(...)` now returns the timed queue
result (`true` when queued or no-op equivalent, `false` for invalid/excluded/full
queue). `source/au2/ArpSIDAUv2Component.mm` records
`auv2RampAnchorScheduledCount` for each generated ramp anchor and
`auv2RampAnchorDroppedCount` when the timed enqueue fails. Immediate parameter writes
remain outside the ramp-anchor counters.

**Validation.** Added default-suite guard `auv2_ramp_anchor_drop_telemetry_v765_tests`
and updated `parameter_dirty_flush_fallback_telemetry_v764_tests` for the bool-return
queue-result shape. Targeted tests passed in this sandbox.


## Fix-order #19 — post-#18 closure/status consistency

**Bug.** After fix-order #17 and #18 were implemented, the earlier audit section still
said parameter-automation sample-accuracy telemetry was deliberately deferred. That
created a handoff split-brain: the source tree contained the dirty-flush fallback
telemetry and AUv2 ramp-anchor scheduled/drop counters, while the audit prose still
claimed the work was pending.

**Fix.** Updated the audit note to say source-level telemetry closure is complete and
kept only the honest macOS-runtime caveat: AUv2/Logic validation still requires a real
macOS install/`auval` session. Added a default source guard so future releases cannot
reintroduce the stale deferred wording or omit the #17/#18 status entries.

**Validation.** Added `post18_closure_status_consistency_v766_tests`, registered in
CMake, and ran it with the version/source hygiene guards.


## Fix-order #20 — source-only release closure / macOS validation handoff

**Bug.** After the concrete source fixes were closed through #19, the handoff still
left an ambiguous "next item" state. In this sandbox, AUv2 install/`auval`, Logic
runtime validation, notarization, and VST3 SDK builds cannot be executed. Without an
explicit closure artifact, a future handoff could accidentally imply that those
platform validations had already run, or could reopen already-closed source-only
items as pending work.

**Fix.** Added `RELEASE_SOURCE_ONLY_CLOSURE.md` and rewrote the pass342 handoff
status so the release state is explicit: source-level audit work is closed through
#20, and the only remaining work is external platform validation on macOS / VST3 SDK.
The status file now lists all closed guards through `SourceOnlyClosureV767Tests` and
keeps the no-false-claims caveat for AUv2/Logic.

**Validation.** Added default-suite guard `source_only_closure_v767_tests`, which pins
pass342 identity, requires the closure document, requires all #12–#20 guards to be
registered in CMake/status, and rejects source-status wording that would claim AUv2
`auval` or Logic validation ran in this sandbox. Targeted source tests passed.


---

## Fix-order #21 — source-root build.sh release helper

**Problem.** The pass342 source-only closure zip was otherwise complete, but it did not include a root-level `build.sh`. That makes handoff/release usage fragile because users have to discover the correct CMake commands and macOS helper scripts manually.

**Fix.** Added executable `build.sh` at the release root. The default path is safe and source-only: configure CMake, build, and run CTest in `./build`. macOS AUv2 install is explicit opt-in via `--install-auv2`, and AudioComponent cache refresh is separately explicit via `--clear-au-cache`.

**Validation.** Added `RootBuildScriptReleaseV768Tests`, which requires the script to exist, be executable, include the expected CMake/CTest/macOS opt-in controls, and avoid risky default behavior such as sudo, Logic force-kill, or unconditional installation.


## pass344 — build.sh release-mode closure

- Closed the regression reported after pass343 full CTest: `FullCTestPreflightScriptV670Tests`, `ReleaseClosureV701Tests`, and `ReleasePackagingV702Tests` failed because the new root `build.sh` omitted the older release-check/package-release contract surface.
- `build.sh` now supports `--release-check` and `--package-release`; package-release forces release-check first and then delegates to `scripts/package_release.sh`.
- Added `RootBuildScriptReleaseModesV769Tests`; isolated validation of the three previously failing tests plus v769 is green.


## Fix-order #23 — macOS install handoff status (pass345)

The user-provided macOS log confirms that the pass344/pass345 source lineage completed a full local CTest run (`313/313` passed), built/installed the AUv2 component, replaced the existing component signature, and triggered the AU cache fast-refresh path. This is now recorded in `RELEASE_MACOS_INSTALL_STATUS.md` and guarded by `MacosInstallStatusHandoffV770Tests`.

This is intentionally not an `auval` or Logic runtime success claim. The remaining external validation step is still `auval -v aumu ArpS ASID` (and the other shipped AU IDs if doing a full release validation), followed by Logic runtime launch/reload.


## Fix-order #24 — macOS closure command (pass347)

Added source-root `build.sh --validate-auv2` / `--strict-auval` so the next macOS validation step is executable, not just documented. The mode is macOS-only, verifies an installed `~/Library/Audio/Plug-Ins/Components/ArpSID.component`, and delegates to `scripts/macos/verify_auv2_component.sh`, which runs targeted strict `auval` for all shipped AUv2 flavors.

Added `RELEASE_AUVAL_COMMAND_STATUS.md` and `RootBuildScriptAuvalModeV771Tests`. This still does not claim AU validation success or Logic runtime success; both remain pending until the corresponding Mac logs are supplied.

## Fix-order #25 — Full macOS closure command (pass347)

Pass347 adds a single executable Mac closure path on top of pass346's targeted auval mode.

- `build.sh --macos-closure` is macOS-only and expands to release-check + full CTest + AUv2 build/install + AU cache refresh + strict targeted auval.
- The command sets `RELEASE_CHECK=1`, `RUN_TESTS=1`, `INSTALL_AUV2=1`, `CLEAR_AU_CACHE=1`, and `VALIDATE_AUV2=1` together, avoiding partial final-validation runs.
- `RELEASE_MACOS_CLOSURE_COMMAND.md` documents the command and the exact truth state: source suite/install/cache refresh are carried forward from the user-provided Mac log; auval and Logic remain pending until logs are captured.
- Guard: `RootBuildScriptMacosClosureV772Tests`.


## Fix-order #26 — macOS build-green handoff (pass349)

Added `build.sh --closure-log-dir DIR` and automatic timestamped log capture for `./build.sh --macos-closure`, so the final Mac closure run can be handed back as a durable transcript instead of terminal scrollback. Guard: `RootBuildScriptMacosClosureLoggingV773Tests`.

## Fix-order #27 — macOS build-green handoff status (pass349)

The user reported that the pass348 lineage builds cleanly on the target Mac ("Det bygget fint"). This is now recorded as a build-green handoff in `RELEASE_MACOS_BUILD_GREEN_STATUS.md` and guarded by `MacosBuildGreenStatusV774Tests`.

This deliberately remains narrower than AU validation: `auval`, Logic runtime validation, VST3 SDK validation, and notarization remain pending until logs are supplied. The next closure command remains `./build.sh --macos-closure --closure-log-dir ./release-logs`.


## Fix-order #28 — macOS closure log command preservation

Status: complete in pass350.

`build.sh` now preserves the original argument vector before option parsing and records the shell-escaped original invocation in `macos-closure-*.log`. This closes a handoff/forensics gap where pass348/pass349 logging captured the output but could record `[ArpSID] command: ./build.sh` after parsing had shifted away all flags.

Validation: `RootBuildScriptClosureLogCommandV775Tests` checks the preserved argv contract and verifies the release docs/status do not claim `auval`, Logic, VST3, or notarization success.

## Fix-order #29 — stale closure guard version assertions (pass351)

Status: complete in pass351.

The pass350 audit found stale release guard assertions that still pinned older closure tests to pass348/pass349/pass350 even though the package had advanced. The affected guard layer has been realigned to the current pass352 package identity while preserving the important truth-boundaries: source/build status may be recorded, but `auval`, Logic runtime, VST3 SDK/toolchain validation, and notarization remain pending until external logs prove them.

This closes the full-suite regression class where release guard tests fail only because their pass-number assertions were stale.

## Fix-order #30 — README current-version note cleanup (pass352)

Status: complete in pass352.

The pass351 guard cleanup fixed stale pass assertions, but the historical README source-folder note still said the canonical version/pass was "currently 0.0.690-pass350". That was a documentation/forensics regression because the release package had advanced beyond pass350. Pass352 updates the note to the current pass352 identity and adds `ReadmeCurrentVersionNoteV776Tests` so this exact stale-current wording cannot return unnoticed. External validation boundaries remain unchanged: `auval`, Logic runtime, VST3 SDK/toolchain validation, and notarization remain pending until logs prove them.


## Fix-order #31 — DIGI AudioQueue weak callback context

Status: complete in pass353.

**Bug.** The CVDisplayLink callback lifetime issue was already fixed with a retained weak context box, but the DIGI AudioQueue REC/MON ingest callbacks still passed the view controller as `(__bridge void*)self`. AudioQueue input callbacks run on CoreAudio-owned threads; a callback that enters during host teardown or rapid REC/MON stop/start could dereference a raw, unretained `ArpSIDViewController*` after the controller had begun teardown. Generation counters protected stale sessions, but they did not make the Objective-C object lifetime safe.

**Fix.** `source/au3/ArpSIDViewController.mm` now uses retained `ArpSIDDigiAQControllerContext` boxes for both preview and record AudioQueue sessions. Each box holds a zeroing `__weak` controller reference. The C callback weak-loads through `ArpSIDDigiAQControllerFromContext_v777(...)`; if the controller is gone it exits without touching freed memory. The retained context is released only after `AQCapture::stop(true)` drains callbacks, and all start-failure/stop/teardown paths release the matching preview/record context.

**Validation.** Added `DigiAudioQueueWeakContextV777Tests`, which rejects raw `(__bridge void*)self` AQ REC/MON contexts, requires the retained weak context boxes, and verifies release paths plus CMake/audit registration. Targeted source guard passed in this sandbox.


## Fix-order #32 — standalone CoreMIDI weak callback context (pass354)

Closed another raw callback-context lifetime surface in `source/au3/ArpSIDHostAppDelegate.mm`.
The standalone CoreMIDI client/input-port/virtual-destination paths passed `(__bridge void*)self` into
`MIDIClientCreate`, `MIDIInputPortCreate`, and `MIDIDestinationCreate`; CoreMIDI can invoke those
callbacks on system threads around teardown.  The refCon is now a retained `ArpSIDHostMIDIContext_v778`
box with a zeroing weak delegate reference.  Notification/read callbacks resolve the delegate through
`ArpSIDHostDelegateFromMIDIContext_v778()` and return safely if the delegate is gone.  The retained box
is released only after the MIDI endpoint/port/client have been disposed, including setup-failure cleanup.

Guard: `StandaloneCoreMIDIWeakContextV778Tests`.


## Fix-order #33 — AQCapture backend callback context (pass355)

Status: complete in pass355.

**Bug.** Pass353 hardened the view-controller REC/MON ingest context, but the lower-level `AQCapture` backend still registered `this` directly with `AudioQueueNewInput` and the `kAudioQueueProperty_IsRunning` listener. The existing queue-pointer/lifecycle gates reduced stale-buffer risk after the callback had reached `AQCapture::handleInput`, but the static CoreAudio ABI trampoline still had to cast raw `userData` to `AQCapture*` first. A callback entering during stop/destruction therefore still had an avoidable raw-object lifetime surface.

**Fix.** `source/au3/ArpSIDDigiAudioQueueCapture.h/.mm` now owns a stable `AQCallbackContext` containing an atomic owner pointer, active-queue pointer, stopping gate, and in-flight callback counter. AudioQueue input/property callbacks receive `&callbackContext_`, increment the context in-flight count before resolving `owner`, reject stopping/stale-queue callbacks, then call into `AQCapture` only if the owner is still present. `stopLocked()` invalidates the context owner/queue before AudioQueue stop/dispose and waits on the context-owned in-flight counter before clearing callback-addressable buffers.

**Validation.** Added `DigiAQBackendCallbackContextV779Tests`, which rejects raw `this` registration/casts in the backend callbacks and requires the context gates, owner invalidation, in-flight accounting, and CMake/audit registration.

## pass356 / Fix-order #34 — PSID async load kernel lifetime

**Bug.** `ArpSIDDSPKernelAdapter` copied PSID bytes before dispatching background work, but the async block still captured `ArpSIDDSPKernel* kernel = _kernel.get()`. Because `_kernel` was owned by the adapter as a `std::unique_ptr`, a host/editor teardown during or before the queued background parse could leave the block with a dangling raw kernel pointer. The request ticket protected stale-result ordering, not object lifetime.

**Fix.** The adapter now owns the kernel as `std::shared_ptr<ArpSID::ArpSIDDSPKernel>` and constructs it with `std::make_shared`. `loadSidFileData:subtune:` snapshots a `std::shared_ptr` before `dispatch_async`, so the background parse/load retains the kernel for the duration of `loadPsidDataForRequest(...)` while preserving the existing copied-payload and request-ticket behavior.

**Validation.** Added `PsidAsyncLoadKernelLifetimeV780Tests`, which rejects the old unique_ptr/raw-pointer capture shape and requires shared kernel ownership, shared capture before dispatch, CMake registration, and audit documentation.

## Fix-order #35 — bank document worker pure helpers (v781 / pass357)

`ArpSIDViewController` async bank import/export workers previously captured a strong controller and called `_loadBankDocumentFromURL:patches:metas:` / `_saveBankDocumentToURL:patches:metas:displayName:` from a global worker queue. ARC made that lifetime-safe, but it still extended editor lifetime and kept file/JSON document work behind an ObjC controller method that should not be part of the worker contract.

The file/JSON load/save logic is now factored into pure helpers (`ArpSIDLoadBankDocumentFromURL_v781` and `ArpSIDSaveBankDocumentToURL_v781`). The ObjC methods remain thin compatibility wrappers. Worker blocks call the pure helpers directly, and controller/UI state is touched only from main-queue publish blocks after weak-loading the controller. Guard: `BankDocumentWorkerPureHelpersV781Tests`.


## Fix-order #39 — bank/preset panel completion URL snapshots (pass361)

Status: complete in pass361.

Bank and preset save/load AppKit completion handlers in `ArpSIDViewController` still used `sp.URL` / `op.URL` directly for metadata stems, IO paths, status filenames, and later publish state after the modal OK result.  This is not a CoreAudio-style UAF, but it was an avoidable UI-object/state coupling: once the panel completes, the durable worker/publish contract should be immutable URL/path/name snapshots rather than repeated reads from an AppKit panel object.

`_drumSaveUserKit:`, `_bankSavePatchJSON:`, `_bankSaveClassicFactoryJSON:`, `_bankSaveBankJSON:`, `_bankSavePatch:`, `_bankLoadPatch:`, `_bankSaveBank:`, and `_bankExportAll:` now copy the selected URL and derived filename/stem/path values immediately and use only those snapshots for file IO, metadata, status messages, and state publication.

Validation: `BankPanelCompletionURLSnapshotV785Tests` checks the snapshot contract and rejects the old direct panel-URL usages in these handlers.

## pass362 / fix-order #40 — C64 SID panel URL snapshot

- Fixed the remaining C64 SID player load-panel completion handler that still used `op.URL` directly for SID file IO and status strings after modal completion.
- `_c64LoadSidFile:` now snapshots `sidURL` and `sidFileName` immediately after OK, then uses those immutable values for `NSData` load, debug-console text, and HUD status.
- Added `C64SidPanelURLSnapshotV786Tests` to prevent regressions back to `dataWithContentsOfURL:op.URL` or `op.URL.lastPathComponent` in the C64 SID load path.

## pass363 / fix-order #41 — DIGI record auto-stop weak continuations

- Fixed DIGI record auto-stop continuations queued from AudioQueue and AVAudioEngine callbacks so they no longer retain callback-local controller references into the main queue.
- Queued stops now use `stopWeakSelf`, strong-load on main, and compare `_digiRecordGeneration_v184_` against the callback/session generation before stopping.
- Guard: `DigiRecordAutostopWeakContinuationV787Tests`.

## pass364 / fix-order #42 — CVDisplayLink main-queue weak continuation

Status: complete in pass364.

The CVDisplayLink output callback was already protected from raw-context UAF by the v756 weak-box, but after `_tryBeginPollTick_v246_` it still assigned `ArpSIDViewController* strongForDispatch = vc;` before `dispatch_async(dispatch_get_main_queue(), ...)`. That retained the editor/controller from the CoreVideo thread until the queued main-thread `_poll` ran, which could unnecessarily extend editor lifetime during host teardown or detach.

The callback now creates `__weak ArpSIDViewController* weakForDispatch = vc;` and the main-queue block strong-loads `strongForDispatch = weakForDispatch`, bailing if nil. If the controller is still alive, `_poll` remains wrapped in `@try/@finally` and `_endPollTick_v246_` is still called.

Guard: `CVDisplayLinkMainQueueWeakContinuationV788Tests`; `Auv2ViewControllerCompileGuardV731Tests` was updated to require the weak main-queue continuation.

## pass365 / fix-order #43 — bank status main-queue weak continuations

Status: complete in pass365.

- Fixed six synchronous bank/preset save/export status publish continuations in `source/au3/ArpSIDViewController.mm` that still captured the handler-local strong controller `s` in a delayed main-queue block.
- Each continuation now weak-loads from the existing zeroing controller token `ws` on the main queue and bails if the editor/controller has already been torn down.
- Added `BankStatusMainQueueWeakContinuationV789Tests`.



## pass366 / fix-order #44 — standalone CoreMIDI notify/menu weak continuations

Closed two remaining standalone-host delayed main-queue lifetime surfaces. `ArpSIDMIDINotifyProc` now derives a zeroing weak continuation token from the retained CoreMIDI context box before dispatching `_reconnectMIDISources` to main, instead of capturing a strong delegate loaded on the CoreMIDI callback thread. `_menuNextPreset:` and `_menuPrevPreset:` now create weak self tokens and strong-load on main before touching `_audioUnit`, removing implicit self capture through ivar access in the delayed preset-setting blocks. Guard: `StandaloneMenuMIDINotifyWeakContinuationsV790Tests`.


## pass367 / fix-order #45 — AU3 requestViewController weak dispatch

Status: complete. `source/au3/ArpSIDAudioUnit.mm` no longer lets the queued `requestViewControllerWithCompletionHandler:` main-thread builder retain/use the AU object through direct `self` capture. It now captures `__weak ArpSIDAudioUnit* weakAudioUnit_v791`, strong-loads it on main, completes with `nil` if the AU was torn down, and passes the weak-loaded AU into both the AUv3 `setExtensionAudioUnit:` path and AUv2 `connectAudioUnit:` path. Guard: `AU3RequestViewControllerWeakDispatchV791Tests`.

## pass369 / fix-order #47 — embedded VST focus weak continuation

`source/gui/arpsid_vst_cocoa_bridge.mm` no longer retains parent/child/handle/controller through the delayed embedded VST focus polish dispatch. The block now weak-loads `weakParent_v793`, `weakChild_v793`, and `weakController_v793` and fail-closes if the embedded editor has already been detached. Guard: `EmbeddedVSTFocusWeakContinuationV793Tests`.


## pass370 / fix-order #48 — standalone startup/window weak continuations

Status: complete in pass370. `source/au3/ArpSIDHostAppDelegate.mm` no longer lets the standalone startup repaint/focus dispatch or delayed window-show polish retain the app delegate through `self->_viewController` / `self->_mainWindow` ivar access. Both queued main-thread continuations now capture weak delegate tokens (`weakSelf_v794_start`, `weakSelf_v794_window`), strong-load on main, nil-guard the required objects, and return if the standalone host has already torn down. Guard: `StandaloneStartupWindowWeakContinuationsV794Tests`.

## pass371 / fix-order #49 — AUv2 view-factory weak AU continuation

Status: complete in pass371. `source/au2/ArpSIDAUv2Component.mm` no longer lets off-main AUv2 Cocoa view construction/disposal continuations retain or dereference the AU object through the raw `audioUnit` local after the caller has timed out. The factory creates `weakAudioUnit_v795`, `buildBlock` weak-loads `strongAudioUnit_v795` before touching associations or connecting the controller, and timeout/orphan disposal also weak-loads before calling `disposeAbandonedAUv2Editor`. Guard: `AUv2ViewFactoryWeakAUContinuationV795Tests`.


## Fix-order #52 — absolute P1 closure audit

**Scope.** Re-audited every P1-labelled source/status surface in the pass374 package: original P1-01..P1-08 closure matrix, legacy pass685 P1 RT-safety notes, current `## P1 — high` and `### P1 — output-tap` sections, P1-11 strict-vs-physical-exactness wording, late P1-13 no-sink POTX/POTY exactness, and P1-20 VST Cocoa factory preset range.

**Result.** All known code-fixable P1 items are closed or explicitly scoped-closed with evidence. No P1 item is left as pending/open/todo in source release status. External release validation remains separate (`auval`, Logic runtime, VST3 SDK/toolchain, notarization).

**Validation.** Added `AbsoluteP1ClosureV798Tests`, a source/status guard that enforces the P1 matrix statuses, required P1 guard registrations, P1-13/P1-20 source wiring, and the RSID strict/physical-exactness split.


## pass375 / fix-order #53 — absolute P2 closure audit

Audited all P2-labelled source/status surfaces and added `AbsoluteP2ClosureV799Tests`.

Result: all known source-code-fixable P2 items are closed, scoped-closed, or accepted-by-design.

- P2-09 remains `NOT_PROVABLE_IN_CONTAINER` by design: Apple AUv2/AUv3/Logic/auval must be proven on macOS with installed bundles and captured logs, not by a source-only test.
- P2-10 remains `FIXED_WITH_SCOPE`: per-cycle CIA/VIC/SID-read closure is guarded, while open-bus capacitance, real ROM identity and chip-sample-dependent unstable silicon variance remain honest physical blockers rather than false exactness claims.
- P2-12 render-adjacent sort is accepted-by-design with explicit RT classification and guard coverage.
- P2-13 canonical event overflow telemetry remains render-published and accessor-backed.
- P2-14 AUv3 render channel snapshot uses per-invocation atomic channel count rather than stale captured format.
- P2-15 Pure-SID capture remains protected by the CAS-based control-side state machine.
- P2-16 non-render drain waits use bounded sleep backoff, not yield spins.

This pass also made P0/P1 absolute closure guards version-robust so future pass bumps do not stale-fail.


## pass378 / fix-order #56 — C64SidPlayer five-flavor P0 closure

Fixed P0 five-flavor regressions found in the fresh pass377 audit: GUI flavor sync now decodes raw flavor 4 via `componentFlavorFromRaw`, AUv2 factory preset cache uses `kComponentFlavorCount` and `componentFlavorIndex`, and AUv2/AUv3/GUI factory-slot policy explicitly handles `ComponentFlavor::C64SidPlayer` as a dedicated default-slot-only product flavor instead of falling through to SID-808/Classic behavior. Guard: `C64SidPlayerFiveFlavorPolicyV802Tests`.

## pass380 / fix-order #58 — render apply uses RT-only hydrated helpers (P0 #5)

- Closed the fresh pass377 audit P0 #5: the render-drained `SidRuntimeModel::applyStateRootBySwap()` path no longer calls helpers whose contract is Non-RT / may allocate (`ensureParameterCapacity`, semantic mirror sync, `sidEnsureSemanticParameterEntries`, `sidHydrateParameterValuesFromSemanticEntries`).
- Added RT-only hydrated-value helpers in `sid_runtime_state_root_presentation.h` and changed the render apply path to use already pre-canonicalized `parameters.values` storage for variant mirrors, D417 mirror derivation, and SID register-image rebuild.
- Non-RT state apply/import/export paths keep their semantic-entry canonicalization behavior; this fix only narrows the audio-thread swap path.
- Guard: `RenderApplyRTOnlyHelpersV804Tests`; behavioral allocation coverage remains `RenderStateRestoreAllocTrapV750Tests`.
