# ArpSID v925 — red-test full closure

V925 closes the six concrete red tests reported after v924:

- `FactoryDrsidKitBankSweepV238Tests`
- `LogicStopStartDrsidKitV238Tests`
- `StateApplyReasonPolicyV240Tests`
- `Sid808RestoreFlavorIntegrationV816Tests`
- `RealtimeSourceLintV817Tests`
- `IngressParityTimingAuthorityV910Tests`

## DrSID transport reset live-edit authority

The v924 DrSID structural reset path still replayed captured/factory DrSID kit parameters after `restoreTransportResetAudioSnapshotImmediate()` had already applied the live host/AU snapshot. That made Logic Stop/Start preserve mode bits but clobber edited kit controls such as Kick Tune, Snare Tone, Drive, Hat Metal and Clap Spread back to factory values.

V925 adds `transportResetOverlayApplied` arbitration:

- transport snapshot overlay owns persistent non-structural audio/kit values;
- structural authority owns only mode/program/bank/root identity;
- DrSID live/factory replay is skipped once the transport overlay has been applied;
- no stale factory replay can overwrite the host/live kit edit image during reset.

## Ring/events held-ledger release parity

Accepted `midiQueue_` NoteOffs previously cleared the held-note mirror at enqueue time, while host `events[]` NoteOffs cleared it at canonical dispatch. That one-block-edge timing difference made NoteOn+NoteOff release sequences fail bit-identical parity.

V925 moves accepted ring NoteOn/NoteOff held mirroring to canonical dispatch, matching host `events[]`. Queue-dropped NoteOff safety fallback still clears held state immediately because it has no later canonical dispatch.

## Source/docs closure

- Removed duplicate raw MIDI Program Change `case 0xC0u` in enqueue validation.
- Restored V816 `P1-13/P1-14` TODO markers.
- Restored V817 `P2-05/P2-06/P2-07` TODO markers.
- Added source-contract guards for the v925 DrSID replay/overlay rule.

Source-side closure: COMPLETE.
