# ArpSID v965 — canonical render-pipeline closure

This release integrates the canonical pipeline repairs on top of the v962-v964 GUI and C64 SIDPLAY telemetry closures without removing those features.

Package: `0.0.690-pass380-v965-canonical-render-pipeline-closure`

## Closed defects

1. Canonical ingress order is no longer overwritten by a second event-type sort.
2. NoteOff, AllNotesOff, AllSoundOff, Panic and transport controls retain bounded queue headroom under dense ingress.
3. Queue replacement recomputes resolved-cycle metadata instead of retaining a stale flag.
4. BitPerfect ARP events share the canonical sample/cycle pipeline. Same-block host MIDI and relevant ARP automation are replayed into a render-local simulation, then its final state is installed after dispatch without double advancement.
5. Render-mode and PAL/NTSC changes use sample-boundary rendering for the transition block, preventing mixed-engine fractional samples and stale-clock slicing.
6. Sequencer boundaries are explicit and drive melodic events, KIT, float DIGI and authentic D418 without collapsing multiple steps into one block-final step.
7. AU and Phase2/VST reverb, limiter and Hi-Fi automation consume exact canonical sample offsets.
8. AU no-output blocks render the complete pipeline into preallocated scratch instead of freezing runtime state.
9. AU telemetry no longer reads live non-atomic render fields; scope frames are accepted only when their frame ID matches the scalar snapshot.
10. Authentic D418 uses the active PAL/NTSC SID clock and can retime while preserving voices.
11. Phase2 preallocates bounded emergency capacity for transient host blocks larger than the declared maximum.
12. VariantChange is applied once to canonical state and once as concrete projection, not twice to the runtime model.
13. AU versus Phase2 overlay support is an explicit compile-time capability contract.

## Validation

The package adds `CanonicalRenderPipelineClosureV965Tests` and a package identity guard, `VersionCoherenceV965Tests`. The merged source registers 479 tests. Eighteen focused tests passed, covering fractional rendering, D418, sequencer swing, KIT routing, telemetry coherence, same-sample policy, state-root staging, ARP audio/gate timing, no-output continuation, the retained GUI v962-v963 closures, C64 SIDPLAY telemetry v964, canonical v965 behavior and package identity.

Platform-specific AUv2/AUv3/Logic, signing/notarization and a build against the real Steinberg VST3 SDK remain external release-platform sign-off requirements.
