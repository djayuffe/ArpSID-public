# ArpSID 0.0.690 pass380 v869 Release Notes

## Summary

v869 closes the post-v868 SynthMode and SID808 audible-authority ledger. It fixes raw-MIDI held replay identity, adds direct-SynthMode stuck-note reconciliation to AU and Phase2/VST, makes SID808 snare body energy audibly guarded, changes SID808 shared-voice filter routing from table-derived to active-drum-derived, and exposes the remaining SID808 audible-authority telemetry needed to diagnose active-but-inaudible bridge states.

## Fixed

- AU raw MIDI held replay now stores stable negative synthetic anonymous note IDs via `SidRuntimeHostSurface::makeSyntheticAnonymousNoteId()`.
- Replayed anonymous SynthMode notes can be released by normal noteId-less NoteOff events.
- Real positive host-noteId voices are still protected from anonymous NoteOff release.
- `VoiceAllocator` now exposes orphan reconciliation that also removes stale held-ledger entries.
- AU direct SynthMode runs the two-pass orphan reconciler once per rendered block when arp and sequencer are disabled.
- Phase2/VST direct SynthMode now maintains a held-key mirror for NoteOn/NoteOff and runs the same reconciler.
- SID808 snare body micro-stage now retriggers with a real gate edge after the snap stage.
- SID808 Snare/Clap/Rim shared voice-1 filter routing now follows the active drum owning that physical voice.
- SID808 bridge telemetry now reports active voice count, below-audible active blocks, literal zero-peak active blocks, and a `SILENT-ACTIVE` authority state.
- SID808 bridge DC telemetry now reports raw pre-DC peak/mean, post-DC peak/mean, DC-blocker coefficient, and reset count.
- SID808 snare-stage telemetry now reports snap/body RMS and peak plus applied/late body-stage counters.

## Guards

- `SynthModeHeldReplayIdentityV869Tests`
- `Sid808SnareBodyRoutingV869Tests`
- retained `Sid808SnareCompleteClosureV868Tests`
- extended `Sid808AudibleAuthorityV865Tests` for silent-active/DC/snare telemetry wiring

## Validation

- Full CTest: 390/390 PASS
- AUv2 build/sign: PASS
- AUv2 install/cache refresh/strict validation: PASS
- Logic/AU cache reset backup: `~/Library/Caches/ArpSID-cleared-logic-au-cache-20260704-160646`
- `auval -a`: lists `ArIn`, `ArpS`, `C64P`, `DrSD`, `S808`
- strict `auval`: PASS for `ArIn`, `ArpS`, `C64P`, `DrSD`, `S808`
- Installed AUv2 SHA256: `6a57b3725c9e8fbe184c038bf6cb1f18e243d120fcba865dce9dd989254adc7e`
