# ArpSID 0.0.690 pass380 — v910 ingress-parity/timing-authority closure

v910 fixes the v909 audit's release blockers in priority order: single-authority
MIDI ingress with audio parity, parent-scope async timing before chunking, the
stopped-transport live-play contract, SID808 selected-kit activation, parameter
exposure, and the timing/split-brain cleanups.

## P0 — AU3 midiQueue_ ring vs events[] parity (was ~42x / −32.6 dB quieter)

Root cause: the transport Play edge ran `clearRuntimeStateForTransportStart_()`
→ `resetTransientRenderState_(true)` → `midiQueue_.clear()` BEFORE the ring
drain, so any live note or knob move enqueued as the transport started was
silently flushed. Host `events[]` (function arguments) were unaffected — the
same GM kick was ~42x quieter through the ring, and velocity-independent
(the note never reached the drum engine at all; only held-mirror side effects
remained).

- Transport Play/Stop edges no longer flush the live `midiQueue_` /
  `paramIntentQueue_` (`resetTransientRenderState_(false)`); factory-root
  restore keeps its own queue scrub.
- Both ingress paths now dispatch through ONE shared helper,
  `dispatchCanonicalIngressEvent_` — suppression policy, GM DrSID promotion,
  the DIGI sample-pad route, held-note mirroring and the canonical push can
  never diverge again. The held-mirror site is carried per event as
  `TimedEvent::ingressProvenance` (ring = mirrored at enqueue, host events =
  mirrored at dispatch) so injected ring events are never double-counted.
- The duplicated raw-MIDI translation was extracted into
  `translateRawMidiRingEvent_` / `translateParamIntentEvent_`, used by both
  the normal drain and chunk normalization.
- The v687 test is now a hard parity contract: same velocity through both
  paths, RMS and peak ratio within the audit's 0.8–1.25 window (measured
  bit-identical), and identical first-audible sample. v910 adds a
  NoteOn+NoteOff sequence parity test (held-mirror/release behavior).

## P1 — Queued MIDI/param timing during large-block chunking

When `numFrames > kMaxFramesPerBlock` (4096), each chunk used to drain the
async rings against chunk-local frame counts and the stale parent
`renderHostTime_`, clamping every event intended for a later chunk into the
first chunk's tail. v910 implements the audit's preferred fix:
`drainAsyncIngressToParentScope_` drains BOTH rings once at parent scope
(hostTime/explicit offsets resolve against the FULL parent block), then the
chunk slicer distributes them together with host `events[]` — in both the
stereo and mono chunk loops. Behavioral regression: a queued NoteOn at parent
offset kMaxFramesPerBlock+2500 lands at exactly that sample, bit-identical to
the events[] path.

## P1 — Stopped-transport live NoteOn suppression

`SidCanonicalHostBlockState` now splits the law:
`suppressTransportSequencerNoteOns` (the old `playStateKnown && !playing`
gate, kept for transport-driven playback and as the deprecated
`suppressHostNoteOns` alias) versus `suppressLiveInstrumentNoteOns`, which is
constitutionally false — live/manual MIDI input plays regardless of transport
state. The AU3 note gates consume the live law, so Classic/Synth/BitPerfect
channel-1 keyboard play works with the DAW stopped, matching VST3/Phase2
(which never transport-gated host notes). Behavioral test: transport stopped +
channel-1 NoteOn through both ingress paths produces audio.

## P1 — SID808 direct MIDI activates the selected factory kit

The direct-MIDI identity repair set only router identity/context; the hit
could still resolve the engine's previous/default voice config. It now passes
the selected factory slot as a `Sid808HitOverride`
(`hasSelectedFactorySlot`), the same per-hit merge path the KIT sequencer
uses, so direct MIDI, events[], midiQueue_ and KIT-scheduled hits all resolve
identical selected-slot kit data. Bridge-level regression pinned.

## P1 — Parameter exposure and stale wording

- `kParamAutoGmDrumPromotion` is exposed in the AU3 parameter tree (DrSID
  Drums group) — VST3/AUv2 already exposed it generically.
- Kernel-level behavioral promotion matrix: Hybrid default does NOT promote,
  Hybrid + opt-in promotes, Instrument never promotes.
- Stale AU3 UI wording corrected: GM channel 10 auto-arms DrSID only when
  Auto GM Drum Promotion is enabled, or in a dedicated drum flavor.
- The stale "484 params" comments now reference kNumParams.

## P2 — Timing/split-brain and telemetry cleanups

- Phase2's legacy double/floor timing helper cluster
  (`sidCyclesPerSampleFloor/Max`, `absoluteSidCycleAtSampleStart_`,
  `clampActualSidCycleOffset_`, `mapCycleOffsetToActualSample_`,
  `absoluteSidCycleToSampleOffset_`, `pushSidWriteDelayed`) is REMOVED — it
  had zero production callers and carried a second timing law parallel to the
  canonical Q32 clock. A source-shape guard keeps it dead.
- `SidWriteQueue` drop telemetry: explicit `droppedThisBlock` (reset at the
  canonical block boundary in `rebaseAfterBlock`), `droppedSinceLastClear`
  (reset by `clear()`), `droppedTotal` (never reset).
- Telemetry snapshot ABI wording clarified: fields are appended to preserve
  prefix offsets for same-version modules; all binary components must be
  rebuilt together when the struct changes.
- DrSID tom-decay default alignment proven by test (engine internal default ==
  projected param default).

## P1/P2 — Honest SID808 kit naming (audit option A)

Slots 120–149 are five authored kit families × six deterministic variant
banks, and are now named that way everywhere: `factorySid808KitName` returns
unique "SID-808 <Family> Kit <A–F>" names, and the patch-bank
`makeSid808KitDefinition` display names use the same family+variant scheme.
Uniqueness and family/variant coherence are tested for all 30 slots.

## Deferred (documented decisions, unchanged behavior)

- Crash lane semantics: Crash/Ride/Splash/China intentionally route through
  the OpenHat cymbal-profile engine on the shared SID voice (option A);
  promoting cymbals to first-class kit voices remains future work.
- DrSID wavetable per-GM-note program variants, `CompiledDrSidKit` runtime
  wiring, KIT override schema extension, KIT step-grid features, the shared
  SID cycle plan, and C64/PSID backlog telemetry formalization remain open
  P2 architecture items.

## Validation

- New `IngressParityTimingAuthorityV910Tests` (behavioral: chunked queued
  MIDI, stopped-transport live play, NoteOn/NoteOff ring-events parity,
  kernel promotion matrix, SID808 selected-slot override, tom default
  alignment; plus source-shape guards) added to CTest and the timing/music
  sweep.
- The strengthened `MidiIngressRingPathV687Tests` parity contract and all
  v902–v909 closure guards remain in the sweep.
