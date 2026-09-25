# ArpSID 0.0.690 pass380 v897 — HOTFIX: projection-instrument playback regression (v894 rebase reverted)

## Regression report

After the v894–v896 installs, synth/projection instruments stopped playing /
responding to transport in the live plugin.

## Root cause

The v894 "cross-block SidWriteQueue unsync" fix added a per-block
`SidWriteQueue::rebaseAfterBlock(frameCount)` call to
`SidRuntimeTargetAdapter::processBlockInto()`. That fix assumed the queue is
fully consumed by `renderSidRegisterQueueToStereo()` within each canonical
block (which is true on the slice-fallback path the unit tests exercise).

In the live AU, however, the FRACTIONAL sub-span render path is active
(stereo output → `tryRenderFractionalSingleSample_` short-circuits the
queue-render slice fallback), so pending write offsets have a
longer-than-one-block consumption lifetime there. Rebasing every block
subtracted `frameCount` from every pending write each block, pulling live
synth-mode register writes (gate/freq/control) to wrong offsets and
corrupting/starving the projected instruments' write stream.

## Fix (v897)

- The per-block rebase call in `processBlockInto()` is REVERTED — playback
  behavior is exactly pre-v894 again.
- `SidWriteQueue::rebaseAfterBlock()` and `SidWriteQueueRebaseV894Tests` are
  retained (method-level pins) for a future, properly-scoped fix of the
  original block-tail stuck-write edge — which must live co-located with the
  actual queue consumer, where the consumption lifetime is known, not at the
  block boundary.
- The original v894-targeted issue (a hard-restart re-gate scheduled past the
  block end by a block-tail note-on can fire late or never) is re-opened and
  documented as a known bounded edge, strictly less harmful than the
  regression the rebase caused.

## Lesson recorded

Unit tests exercised the slice-fallback queue-render path only; the
fractional path has no standalone test that plays a note through
`processBlockInto` end-to-end. Adding such an end-to-end synth-mode
render test is the prerequisite for re-attempting the stuck-write fix.

## Verification

- Full CTest: **442/442 PASS** after the revert.
- AUv2 rebuild/install/clear-cache/strict-auval: see STATUS.md for the
  installed hash and per-subtype results.
- On-device confirmation that projection instruments play again is the
  user-side gate for this hotfix.
