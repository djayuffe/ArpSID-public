# C64 / SID808 / SynthMode Closure v871

## Problem

The v870 package fixed the obvious SID808 instrument-shape problems, but the deeper audit still found correctness gaps in the authority path below audible output:

- C64 render rollback only restored a narrow bridge/sink subset and could leave PHI2, platform RAM, open-bus, CIA/VIC/CPU, timed-write, D418, or diagnostic state partially advanced after a failed render slice.
- Dirty PHI2 write-log wrap could force recurring full 64 KB platform syncs because wrapped logs were not consumed with transaction semantics.
- The PSID play-call cap could skip due calls without direct dropped-call telemetry, making choppy or missing-gate behavior hard to distinguish from emulation cost.
- SynthMode replay still lacked canonical `voiceToken` on the event itself, so held replay and NoteOff could drift back to channel/note/noteId identity.
- SID808 default musical-shape tests did not prove the same invariants across all canonical factory kits.

## C64 Transaction Authority

`C64Runtime::beginRenderTransaction()` now captures:

- `C64Phi2Machine::Snapshot`
- `C64RuntimeSidSink::Snapshot`
- the platform render mutation journal

The AU kernel bridge transaction also captures `C64SidBridgeState::Snapshot`. The stack token is intentionally small; the large bridge snapshot is object-owned storage, not a render-stack blob.

Rollback now restores:

- full PHI2 machine state, including memory matrix, CPU, CIA, VIC, port/reset/open-bus state, SID sink hooks, diagnostics, and trace state
- runtime SID sink state, including registers/readback/counters
- bridge timed writes/registers/readback/D418/defer/overflow state
- platform RAM mutations through the bounded journal

Commit now consumes the platform journal and performs any deferred full RAM sync only after the transaction is known to be kept.

## Dirty-Log Wrap Semantics

When `syncPlatformRamFromPhi2Writes_()` sees a dirty-log wrap inside an active render transaction, it sets a deferred full-sync flag instead of immediately touching the whole platform RAM image. That keeps rollback bounded to the journal and PHI2 snapshot.

On commit, the deferred full sync is applied once. On rollback, the PHI2 snapshot is restored and the deferred flag is cleared. This prevents one wrapped dirty log from becoming a permanent 64 KB sync trigger.

## Play-Call Cap Telemetry

The render loop now publishes:

- play-call cap hit count
- dropped due calls in the last capped block
- max dropped due calls in any capped block
- total dropped due calls
- platform rollback proof failure count

This turns the old "missing gates or choppy SIDPLAY" symptom into visible counters.

## SynthMode Voice Tokens

Canonical voice identity now travels with the event:

- `SidTimedEvent::voiceToken`
- AU `TimedEvent::voiceToken`
- AU canonical queue conversions preserving the token
- held replay stamping NoteOn and release events with the token
- explicit token binding in the runtime model and voice allocator
- SynthMode scheduler `noteOnWithToken`
- NoteOff token-first release

This keeps anonymous replay compatibility while preserving the real-host-note-id protection added in the v869 stuck-note work.

## SID808 Factory Musical Shape

v871 keeps v870's musical programs and adds all-slot factory authority:

- Kick factory configs use triangle source shape for sweep ownership.
- Kick staged pitch movement forces triangle and pulse width 0.
- Tom staged pitch movement forces triangle and pulse width 0.
- OpenHat is checked for a real ring window across factory slots.
- Clap is checked for burst/tail behavior across factory slots.
- Cowbell is checked for ring tail without a late bloom after release.
- Explicit per-hit waveform overrides remain valid and are guarded by the retained override tests.

## Regression Coverage

New guards:

- `C64RenderTransactionV871Tests`
- `SynthModeVoiceTokenV871Tests`
- `Sid808FactoryMusicalShapeV871Tests`

Updated guards:

- `RenderBridgeSnapshotStackGuardV806Tests`
- `RealtimeSourceLintV817Tests`

Retained adjacent guards:

- `Sid808MusicalShapeV870Tests`
- `Sid808HitOverrideV626Tests`
- `KitSid808FactoryVoicePrecedenceV637Tests`
- `SynthModeHeldReplayIdentityV869Tests`
- `Sid808SnareBodyRoutingV869Tests`
- `SidplayRegisterEngineV867Tests`

## Validation

- Full release build PASS
- Full CTest 394/394 PASS
- AUv2 install PASS
- Strict installed AUv2 validation PASS
- AU/Logic component refresh PASS
- `auval -a` lists `ArIn`, `ArpS`, `C64P`, `DrSD`, `S808`
- Installed AUv2 binary SHA256 `2249ae85f5610e57e359951719bae609bc589fe060b9b57adbe4530027250da0`
