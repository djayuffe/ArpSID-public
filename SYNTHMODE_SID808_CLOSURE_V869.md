# v869 SynthMode and SID808 Closure

## Root Issues

The remaining P0 SynthMode issue was identity drift. AU raw MIDI held replay used a positive serial as a replay note ID. Raw MIDI NoteOff is anonymous (`noteId == -1`), so a replayed voice carrying a positive fake ID could survive the release path, especially in mono/guitar-style SynthMode.

The remaining SID808 P1 issue was audibility and authority. v868 made the snare body register-visible, but the delayed body stage reused the existing gate instead of creating a new body edge. Shared-voice routing also still had a table-derived inheritance risk for Snare/Clap/Rim on SID voice 1.

## Fixes

- Added `SidRuntimeHostSurface::makeSyntheticAnonymousNoteId()` and changed AU held replay to store negative synthetic anonymous IDs.
- Added `VoiceAllocator::reconcileUnheldVoices()` so the allocator can release confirmed orphan voices and remove the matching held-ledger token.
- Added AU and Phase2/VST direct-SynthMode reconciliation hooks. They run only in direct SID-register mode with arp and sequencer disabled, and keep the existing two-pass grace period.
- Added Phase2/VST held-ingress mirroring for NoteOn/NoteOff so the reconciler has a real host-held authority.
- Changed SID808 snare body micro-stage application to retrigger with `raiseGate=true`.
- Added active per-voice SID808 drum/filter state. Filter routing now follows `activeDrumByVoice_`, `activeFlagsByVoice_`, and `activeFilterModeByVoice_`.
- Added bridge-owned SID808 silent-active telemetry: active voice count, below-audible active block count, literal zero-peak active block count, and a GUI-visible `SILENT-ACTIVE` authority state.
- Added SID808 raw/post-DC bridge telemetry from the kernel replacement path: pre-DC peak/mean, post-DC peak/mean, blocker coefficient, and blocker reset count.
- Added render-published snare micro-stage telemetry: snap/body RMS, snap/body peak, applied body-stage count, and late body-stage count.

## Tests

- `SynthModeHeldReplayIdentityV869Tests` proves negative synthetic IDs, anonymous release compatibility, real host-ID protection, allocator held-ledger cleanup, AU source shape, VST held mirror, and block-end reconciliation.
- `Sid808SnareBodyRoutingV869Tests` renders a snare and requires snap RMS, body RMS, body peak, body/snap ratio, and matching engine-published snare telemetry. It also verifies Snare owns voice 1 with BP+HP routing, then Clap/Rim replace that owner and clear the route.
- `Sid808AudibleAuthorityV865Tests` now also proves the silent-active telemetry path, zero-peak counter exposure, raw/post-DC field wiring, snare body RMS field wiring, and the `SILENT-ACTIVE` GUI authority label.

## Release Validation

Full CTest passed 390/390. AUv2 built, signed, installed, and validated. Logic/AU caches were moved aside to `~/Library/Caches/ArpSID-cleared-logic-au-cache-20260704-160646`. Post-cache-reset `auval -a` saw all five subtypes, and strict `auval` passed for `ArIn`, `ArpS`, `C64P`, `DrSD`, and `S808`.

Installed AUv2 binary SHA256:

```text
6a57b3725c9e8fbe184c038bf6cb1f18e243d120fcba865dce9dd989254adc7e
```
