# ArpSID v968 — DrSID user-kit save normalization + full-suite green

This release closes the remaining fixable items from the DrSID/kit/GM audit and repairs the four pre-v965 tests that had drifted out of sync with the canonical render-pipeline contracts, on top of the v967 quick-kit base-parity closure.

Package: `0.0.690-pass380-v968-drsid-kit-save-and-queue-test-closure`

## Closed defects

- **DrSID user-kit SAVE did not normalize render mode.** `_drumSaveUserKit` stamps `meta.role = Drum` but the save serialized the live kernel shadow verbatim, unlike the import/export bank paths which always force `DrSidEnable=1` / `SynthModeEnable=0` into the saved root. A kit saved while the engine was in another render mode was stored as a drum-role patch (so it appeared in the drum user-kit library) yet loaded back without entering DrSID mode. The save now routes through a dedicated `_saveDrumKitDocumentToURL` that normalizes the saved document into DrSID mode for both `.arpsid` and `.json`.

## Test suite repaired to the current contracts (previously 476/480)

Four pre-v965 tests still asserted contracts that the v965 canonical render-pipeline closure deliberately superseded. Each is re-pinned to the *current* behavior (not weakened):

- `ReleaseFinalClosureV404Tests` / `ReleaseRuntimeClosureV408Tests` — asserted a full-capacity low-priority queue fill and event-type-priority sort ordering. v965 PIPE-013 reserves headroom (`kReleaseReserve`) so low-priority (pitch-bend/CC/note-on) traffic is admitted only up to `capacity − reserve`, and PIPE-001 makes canonical `arrival_order` authoritative — only emergency sample-boundary kills (Panic/AllSoundOff/AllNotesOff) preempt, and only among resolved same-sample events. The tests now verify the reserve boundary, release-critical replacement at true capacity, emergency-kill preemption, and that a plain NoteOff no longer reorders ahead of earlier arrivals.
- `ClassicModeAuthorityClosureV909Tests` — asserted a live-local telemetry ARP/SEQ authority spelling. v965 publishes the effective ARP/SEQ authority into atomics (`telemetryArpEnabled_` / `telemetrySeqEnabled_`) via the shared `sidEffective*AuthorityFromLiveParams` helpers, and the snapshot reads those atomics. The invariant (telemetry authority derives from the shared helper, never raw `kParamArpEnable`) is unchanged; the assertion now matches the published-atomic implementation.
- `GuiViewControllerWiringV590Tests` — pinned a brittle single-line spelling of the multi-line `renderDigiSamplerLayer_(outputs, …)` call; re-pinned to the call site plus the `int outputChannelCount` signature contract.

## Regression coverage

- `DrSidUserKitSaveModeNormalizationV968Tests` pins that `_drumSaveUserKit` saves via `_saveDrumKitDocumentToURL` and that the drum-kit save forces DrSID mode into the root for both formats.
- All four repaired tests pass; the focused suite is green.

## Validation limits

- Focused source/runtime validation; full macOS AU/Logic host validation, signing and a real Steinberg SDK VST3 build remain external sign-off items (unchanged).
