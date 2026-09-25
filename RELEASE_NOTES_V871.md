# ArpSID 0.0.690 pass380 v871 Release Notes

## Summary

v871 closes the deeper audit left after the v870 SID808 musical-shape release. v870 made the missing drum shapes audible and measurable; v871 fixes the remaining correctness layer below it: C64 render rollback authority, dirty-log consume semantics, explicit play-call cap telemetry, SynthMode canonical voice-token identity, and all-slot SID808 factory musical-shape invariants.

This release is best described as the C64/SID808/SynthMode transaction closure. It is intended to prevent silent SIDPLAY state divergence, missing gates after rollback, permanent full-RAM sync pressure after dirty-log wrap, anonymous SynthMode replay stuck notes, and factory SID808 regressions where a drum sounds correct only in the default engine path but not across factory slots 120..149.

## Fixed

- C64 render mutation is now transaction-owned by `C64Runtime` and the AU kernel together.
- `C64Phi2Machine` has a complete snapshot/restore surface for CPU, CIA, VIC, memory matrix, open bus, reset, port, diagnostics, SID sink, and trace state.
- `C64RuntimeSidSink` snapshots SID registers, per-chip register banks, OSC3/ENV3 readback, write counters, invalid/open-bus/POT/hole counters, and last write/read fields.
- `C64SidBridgeState` snapshots timed writes, per-chip registers, readback mirrors, D418 counters/state, engine/mirror/defer flags, overflow/count telemetry, and last write metadata.
- AU bridge rollback restores the C64 SID bridge from a heap/member snapshot while `C64Runtime` owns PHI2/sink/platform rollback.
- Dirty PHI2 write-log wrap inside an active render transaction now defers the 64 KB platform RAM sync until commit instead of forcing rollback to touch more RAM than the journal can prove.
- Rollback clears the deferred full-sync state by restoring the PHI2 snapshot, so a wrapped dirty log cannot permanently force full 64 KB copies in later blocks.
- C64 play-call cap pressure now reports cap hits, dropped due calls for the last capped block, max dropped due calls, and total dropped due calls.
- Platform rollback proof failures are counted and published.
- `SidTimedEvent` carries `voiceToken` as canonical event identity.
- AU canonical `TimedEvent` conversion preserves `voiceToken` in both directions.
- Held replay stamps replayed NoteOn and AllNotesOff/NoteOff events with the original voice token.
- SynthMode scheduler and voice allocator can bind an explicit known token instead of allocating a new token for replay.
- SynthMode NoteOff uses `ev.voiceToken` first and falls back to channel/note/noteId lookup only when no canonical token is present.
- SID808 factory Kick source config no longer uses pulse width/pulse waveform as the factory basis for the staged pitch sweep.
- Kick and Tom staged microprograms force triangle waveform and pulse width 0 for the staged pitch movement.
- Cowbell tail/release level is reduced so the ring decays instead of blooming late.
- SID808 factory slots 120..149 are now rendered/inspected for Kick, OpenHat, Clap, Cowbell, and Tom behavior instead of relying only on one default-path musical-shape smoke test.
- Explicit per-hit SID808 waveform/control-bit overrides remain valid.

## Guards

- `C64RenderTransactionV871Tests`
- `SynthModeVoiceTokenV871Tests`
- `Sid808FactoryMusicalShapeV871Tests`
- updated `RenderBridgeSnapshotStackGuardV806Tests`
- updated `RealtimeSourceLintV817Tests`
- retained `Sid808MusicalShapeV870Tests`
- retained `Sid808HitOverrideV626Tests`
- retained `KitSid808FactoryVoicePrecedenceV637Tests`
- retained `SynthModeHeldReplayIdentityV869Tests`
- retained `Sid808SnareBodyRoutingV869Tests`

## Validation

- Full release build: PASS
- Full CTest: 394/394 PASS
- AUv2 build/sign: PASS
- AUv2 user install: PASS
- Strict installed AUv2 verification: PASS
- `auval -a`: lists `ArIn`, `ArpS`, `C64P`, `DrSD`, `S808`
- AU/Logic component refresh: PASS
- Installed AUv2 binary SHA256: `2249ae85f5610e57e359951719bae609bc589fe060b9b57adbe4530027250da0`

## Release Files

- Detailed closure doc: `C64_SID808_SYNTHMODE_CLOSURE_V871.md`
- Patch artifact: `dist/ArpSID-0.0.690-pass380-v871-c64-sid808-synthmode-release.patch`
- Source artifact: `dist/ArpSID-0.0.690-pass380-v871-c64-sid808-synthmode-release.zip`
