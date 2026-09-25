# ArpSID 0.0.690 pass380 v898 — SynthMode audibility P0 closure

## The P0

Projection/SynthMode instruments were **silent and appeared to ignore MIDI**.
Confirmed by an end-to-end kernel repro (note-on → processBlock → output):
BitPerfect 0.054 peak, DrSid 0.108 peak, **SynthMode 0.000**. The pristine
v892 tree reproduced it identically — the regression was introduced between
v875 and v892 (v874 was the last installed build where SynthMode audibly
played; every closure since was verified only by unit tests, never by playing
a note through the kernel). Four stacked defects each independently silenced
the path; all four are fixed:

### 1. Note-routing state split-brain (routing authority vs engine guards)

`runtimeKernelOnMidiNoteOn` resolves the render mode from **state-root**
params (`isSynthModeEnabled()` → `resolveRenderMode()` reads
`state_root_`), while the engines' note guards read **renderParams_**. Host
snapshot restores (`restoreHostParameterSnapshotImmediate`,
`restoreTransportResetAudioSnapshotImmediate` — the paths Logic replays at
Stop/Play — and `runtimeStageNormalizedParameterOnly`) updated only
`params_`/`renderParams_`. With a stale state root, a SynthMode note routed
to the BitPerfect fallback and was dropped by the live-param guard. All
three paths now mirror into `runtimeModel_.applyAutomationPoint()`.

### 2. Unreachable write-queue consumer (the v886/v887 structural break)

SynthMode register writes go into `bank.sidWriteQueue`, whose only consumer
was `renderSidRegisterQueueToStereo()` on the slice-fallback render path.
The v886/v887 dispatcher closure made fractional-capable backends always
render through the per-sample interval path, which marks every sample
fractional-active — so the slice fallback became unreachable and **note
writes never reached the engine at all**. New
`runtimeTransferDueSynthWritesToBackend()` (sid_runtime_fractional_render.h)
is the fractional path's queue consumer: due writes transfer into the
engine's internal subphase queue each sample and are erased from the block
queue; future-sample writes stay pending.

### 3. Interval-tiling gap swallowed the GATE-ON write

`SidRegisterEngine::renderIntervalAccurate` applied queued writes only
inside each requested interval's [begin..end) window. The kernel's interval
tiling legally skips positions (a whole-cycle window ending at cycle N
followed by a subphase span starting at (N, s>0) never covers (N, 0)); the
transferred gate-on write landed exactly in such a gap, was never applied,
and was wiped by `resetIntervalCursor()` at the sample boundary — voices
stayed gate-off with correct-looking registers. The interval render now
first applies any queued write positioned before the interval begin (at
most one cycle early; previously never).

### 4. Orphan reconciler killed every host-event voice

The v869 two-pass reconciler judges "held" from the raw-MIDI held-ingress
ledger (`mirrorMidiHeldIngress_`), which only the raw-MIDI byte path fed.
Notes delivered as host timed events — Logic region playback — never
registered, so each voice they started was declared an orphan on the next
per-block reconcile and hard-gated off (envelope forced to 0 with registers
intact). Host timed note-on/off events now mirror into the ledger with
synthetic status bytes.

## Verification

- New `KernelE2EAudibilityV898Tests` (real `ArpSIDDSPKernel`, note-on
  through `processBlock` per render mode, requires audible output):
  BitPerfect 0.054, **SynthMode 0.472**, DrSid 0.108 — PASS.
- Full CTest: **443/443 PASS**.
- AUv2 build/install/clear-cache/strict-auval: see STATUS.md.

## Process lesson (recorded in project memory)

Unit-level closures cannot gate audibility. This is the first end-to-end
"note in → sound out" test in the suite; it would have caught the v886
break the day it landed. Keep it in the release closure suite forever.
