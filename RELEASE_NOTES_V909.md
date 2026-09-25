# ArpSID 0.0.690 pass380 — v909 Classic-mode authority closure

v909 closes the remaining P0/P1 items from the v908 audit: Classic-mode render
authority, VST3 projection-mirror telemetry truth, no-output telemetry capture,
plus the requested SID-808 kit/tom improvements and the HMOS-II 8580-class SID
default.

## P0 — Classic/BitPerfect can no longer be hijacked by GM drum auto-promotion

Previously every NoteOn in VST3/Phase2 (and the Hybrid AU flavors) called the GM
DrSID promotion path unconditionally, so a host track on MIDI channel 10 with
notes 35–81 silently switched a user-selected Classic/BitPerfect (or SynthMode)
session into DrSID — making Classic appear broken.

- New parameter `kParamAutoGmDrumPromotion` ("Auto GM Drum Promotion"),
  **default OFF**, appended as ParamID 511 (kNumParams 511 → 512; state blobs
  remain forward/backward compatible because the codec carries paramCount).
- New shared law `sidCanonicalGMDrumAutoPromotionAllowed(dedicatedDrumFlavor,
  hybridFlavor, paramOn)` in `sid_runtime_host_policy.h`:
  - DrumMachine / SID-808 flavors: always allowed (drum machines by construction).
  - Hybrid (Classic): only when the user enables Auto GM Drum Promotion.
  - Instrument / C64SidPlayer: never.
- `sidCanonicalEvaluateGMDrSidPromotion` / `sidCanonicalApplyGMDrSidPromotion`
  now require the explicit `autoPromotionAllowed` authority; gated in all three
  wrappers (VST3 Phase2 ingress, AU3 kernel, AUv2 component).
- Regression pinned: **Classic mode + MIDI channel 10 + note 36 must not enable
  DrSID unless Auto GM Drum Promotion is enabled** (behavioral test with a mock
  promotion target plus source-shape guards on every wrapper call site).

## P1 — Phase2 projection mirror is no longer a silent no-op

`ArpSIDProcessorPhase2::runtimeMirrorAppliedProjectionWrite` remains a no-op by
design (Phase2 does not own the AU3 C64 telemetry mirror), but telemetry now
says so explicitly instead of letting the UI imply mirror accuracy:

- `ArpSIDTelemetry` gains `projectionMirrorAvailable` and
  `projectionMirrorBackend` (`kArpSIDProjectionMirrorBackendUnavailablePhase2`
  for VST3/Phase2, `kArpSIDProjectionMirrorBackendAU3Kernel` for the AU3
  adapter, which publishes its real kernel mirror sink).

## P1 — No-output telemetry no longer lies after scratch render

The v908 no-output branch rendered scratch and advanced FX correctly but still
published zero meters/scope. v909:

- Calls `captureTelemetryMeters(tmpOutL, tmpOutR)` and publishes the scratch
  buffers as the scope source in the no-output branch, so meters/scope reflect
  the actually-advancing engine.
- Publishes `noOutputBusActive = true` and
  `telemetryRepresentsHostOutput = false` so diagnostics know the provenance.

## P1/P2 — No-output closure is now behavioral, not only source-shape

`classic_mode_authority_closure_v909_tests` proves behaviorally that:

- an engine sequence produces bit-identical audio whether a middle block
  renders into host buffers or into discarded scratch buffers (state continuity
  is destination-independent), and
- shared post-FX (SchroederReverb) state ages identically through a discarded
  scratch block — the reverb tail after the no-output block is bit-identical to
  the all-host-buffer reference run.

## SID-808 default kit and tom improvements

- All five factory kit families (Classic/Punch/Lo-Fi/Hard/Wide) gained longer,
  deeper singing toms: longer decay/release nibbles and higher tom level per
  kit character.
- The engine default tom voice got a longer decay/release and more level
  (`0x04/0x79`, level 0.95).
- The three-stage tom pitch-drop program now drops deeper with longer stage
  spacing and a stronger sustained tail (mid/low/high tom profiles), and the
  tom one-shot hold window grew 240 ms → 300 ms so the tail can breathe.
- DrSID (Hybrid drum engine) tom defaults follow: `kParamDrSidTomDecay`
  default 0.34 → 0.48 with the engine default matched.

## HMOS-II 8580-class SID is the default everywhere

- `kParamSidChipRevision` default is now selector index 3 — **MOS 8580 R5**
  (the HMOS-II "new SID"/8580-class die) — via the new canonical constant
  `kSidChipRevisionDefaultIndex`. Factory patches seed from this default.
- The forensic revision legacy mirror default now also resolves to R5, and all
  GUI fallback centers use the canonical default constant instead of a
  hard-coded 6581 R3 index.
- The legacy SID model mirror (`kParamSidModel`), the `$D41D` system byte, and
  every engine (BitPerfect, SID-808, SID-register) already defaulted to
  MOS 8580; the selector was the last 6581-leaning default.
- The 6581 ADSR-bug quirk remains off by default.

## Validation

- New `ClassicModeAuthorityClosureV909Tests` added to CTest and to
  `scripts/run_timing_music_contract_sweep.sh`.
- v904–v908 lineage guards extended to accept the v909 package identity; the
  v908 no-output source contracts remain pinned and intact.
