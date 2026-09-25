# SID-808 Musical Shape Closure v870

## Problem

The v869 bridge/audio-authority work proved that SID808 hits reached the bridge and that the snare body could not silently collapse. It did not prove that every SID808 drum had the right musical motion. The remaining audible failures were concentrated in the non-snare drum shapes:

- Kick rendered as a mostly static low voice, without a real falling pitch sweep.
- OpenHat had attack energy but did not keep a useful ring.
- Clap behaved like one noise tick instead of a burst train.
- Cowbell used one weak pulse partial and barely rang.
- Tom stayed too high/static and lacked the expected pitch drop.

## Engine Fix

`Sid808Engine` now owns a bounded four-stage microprogram per physical SID voice. Each stage stores an absolute sample delay, drum owner, SID voice config, level, and whether the stage should retrigger the gate. The render loop chunks at the next pending micro-stage or auto-release boundary, so register changes land during rendering instead of waiting for the next host block.

This keeps the existing fixed-voice SID808 ownership law:

- Kick/Tom use SID voice 0.
- Snare/Clap/Rim use SID voice 1.
- Hats/Cowbell use SID voice 2.
- Snare still has its real snap-to-body gate edge from v869.

## Drum Programs

Kick:

- Initial triangle attack starts well above the body pitch.
- Three non-gated stages drop through punch, body, and tail frequencies.
- The hold window is extended to 130 ms so the tail is not cut short.

Tom:

- Initial triangle program starts above the authored body pitch.
- Three non-gated stages drop into body and tail frequencies.
- The default tom body is lowered and the hold window is extended to 240 ms.

OpenHat:

- Noise stays the waveform throughout the staged program.
- The default open hat opts into filter routing.
- High-pass shaping plus staged level/frequency changes keep a measurable ring and late tail.
- The hold window is extended to 420 ms.

Clap:

- The first noise hit is followed by scheduled gated bursts at short millisecond offsets.
- A lower noise tail follows the burst train.
- The hold window is extended to 95 ms.

Cowbell:

- Pulse partial A is followed by scheduled pulse partial B and alternating pulse-width/frequency stages.
- Cowbell opts into band-pass filter shaping.
- The final stage is a lower-level ringing tail.
- The hold window is extended to 360 ms.

## Boundary Fix

SID808 `allNotesOff()` now force-idles the three SID voices, clears pending micro-stages, clears auto-release state, and clears filter routing. This prevents old long hat/cowbell/tom release tails from leaking into a later explicit mute/setup boundary, which was caught by `Sid808AudibleAuthorityV865Tests` after the v870 tails were lengthened.

## Regression Coverage

`Sid808MusicalShapeV870Tests` verifies:

- Kick applies at least three pitch-sweep stages, starts above body pitch, ends below body pitch, and does not retrigger the gate during the sweep.
- Tom applies at least three pitch-drop stages, starts above body pitch, ends below body pitch, and does not retrigger the gate during the drop.
- OpenHat applies a multi-stage ring program and keeps measurable ring/late-tail RMS.
- Clap applies three scheduled burst/tail stages and uses gate retriggers.
- Cowbell applies alternating pulse partial stages, changes away from the first partial, remains pulse-based, and keeps a measurable ring.
- Source guards keep the bounded micro-stage program and each named drum program present.

Validation:

- `Sid808MusicalShapeV870Tests` PASS
- SID808 closure cluster 5/5 PASS
- Full CTest 391/391 PASS
- AUv2 install/strict validation PASS
- Installed AUv2 SHA256 `6c8a5f336c7e73643b7037d8fc05755c76e1ad509897dcc1910dae0d902a7042`
