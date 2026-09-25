# ArpSID 0.0.690 pass380 v894 — sync/drift audit + write-queue rebase closure

Full audit of every clock/sync authority that could make audio drift, detune,
or split from its timeline, plus the SID projection surfaces. One genuine
unsync defect found and fixed; every other surface verified clean.

## Fixed — cross-block SidWriteQueue unsync (dead re-gate / stuck TEST)

`SidWrite.sampleOffset` is block-local. Writes scheduled past the current
block's end — exactly what `pushSynthModeWriteDelayedByCycles()` produces for
hard-restart re-gates and delayed TEST releases when a note-on lands on the
block's tail samples — survived the render's `eraseIf()` but kept their
old-block offsets. In the next block they fired a full block late, or (at
typical block sizes) **never**: the local sample index never reached them, so
the re-gate/TEST-release write sat in the queue forever — a dead gate or a
permanently TEST-muted voice, plus a stale entry the sorter re-touched every
slice.

Fix: `SidWriteQueue::rebaseAfterBlock(blockFrames)` shifts every surviving
write into the next block's timeline (offsets clamp to 0, so nothing is ever
lost or fires early), called once per canonical block from the single shared
entry `SidRuntimeTargetAdapter::processBlockInto()` — covering the AU kernel
and the Phase2/VST processor with one authority.

## Audited clean (no change needed)

- **`SidCycleClockState`** (sid_event_timing.h): Q32 fixed-point
  cycles-per-sample with the fractional remainder carried across samples and
  blocks; remainder reset only on a real rate/clock change. No rounding drift.
- **`C64TimingMath`**: PAL/NTSC PHI2, VIC-frame and CIA-latch constants all
  static_asserted; the deliberate VBI-vs-CIA cadence split (19656 vs 19705
  PAL cycles) is documented and never mixed.
- **C64 SIDPLAY block mapping** (kernel): timed-write → sample bucketization is
  Q32-exact from a monotone audio host-sample cursor
  (`c64PsidAudioHostSampleCursor_`, advanced exactly once per rendered block);
  continuous/CIA/VBI timeline bases are captured per block; future-write
  clamps and negative-base deltas are counted, not silently swallowed.
- **Live CIA cadence**: CIA-speed tunes re-read the live Timer A latch per
  block (multi-speed tunes track correctly); VBI tunes use physical VIC frame
  geometry. Passive PHI2 debt is capped to the block span (v841 law intact).
- **`SidHostCycleDispatcher`**: fractional-capable backends always advance
  through the physical per-sample cycle clock (v886–v892 laws verified);
  event metadata cannot select a different render clock law.
- **Projection engine clocks**: `projectRuntimeStateToBackends()` pushes one
  `sidClockHz` (from the variant profile) to BitPerfect, DrSID and the
  register engine; the C64 SIDPLAY path re-pins each chip engine's system
  byte (PAL/NTSC + model bits) and `setClockFrequency(platform.clockHz())`
  every block, so a synth-side variant change can never detune a loaded tune.
- **Projection write authority**: audio writes flow queue→engine only;
  the C64 telemetry mirror is fed solely from the applied-write observer of
  `renderSidRegisterQueueToStereo()` (v874 law verified still single-sourced,
  demand-gated, block-local per v885). `SidNoopAppliedWriteObserver` paths
  unchanged.
- **Forensic clock jitter/thermal/ripple detune** in `SIDChip` is intentional
  character, fully frozen by `bitPerfectMode`/`forensicFreeze` (verified the
  frozen path zeroes jitter, ripple and thermal terms).

## Deliberately unchanged (documented decisions, re-affirmed)

- v874 P0-4 (re-anchor to host `mSampleTime`): the C64 player is a
  free-running monotonic timeline by design; transport re-anchoring risks
  introducing the very discontinuities it claims to fix.
- v874 P0-5 (audio-vs-telemetry cursor split): the audio cursor became the
  sole authority in v886; telemetry's cosmetic cursor cannot influence it.
- The full `SidProjectionWriter` refactor remains deferred pending on-device
  Logic verification — the projection audit above found its *behavior*
  correct; the refactor is architecture, not a defect.

## Added tests

- `SidWriteQueueRebaseV894Tests`: beyond-block writes survive the block,
  rebase by exactly the block length, and are consumed in the following block
  (previously stuck forever); pathological below-block leftovers clamp to
  sample 0; empty/zero-frame rebase is a no-op.

## Verification

- `SidWriteQueueRebaseV894Tests` PASS; `SidProjectionAppliedWriteObserverV874Tests` PASS.
- Projection/synth/SID-808 family sweep: 37/37 PASS.
- Full C64/PSID/RSID/PHI2/CPU family sweep (incl. v893+v894 suites): **130/130 PASS**.
- AUv2 build/install/clear-cache/strict-auval: see STATUS.md for the result of
  this pass's build.
