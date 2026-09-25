# ArpSID v945 — authority staging policy closure

Version: `0.0.690-pass380-v945-authority-staging-policy-closure`

This release hardens the v943/v944 authority canonicalization work by closing the
remaining staging-policy gaps found in the v943 audit.

## Fixed

- AU3 `runtimeStageNormalizedParameterOnly()` now uses `sanitizeNormalizedParamValue()`
  with the parameter id and default value, matching the Phase2 staging law.
- AU3 no longer uses generic clamp-only staging for canonical param/model/render sync.
- Phase2 `runtimePolicyHandleSynthModeEnable()` no longer writes raw `paramValues[]`
  directly for forced ARP/SEQ clears.
- Phase2 SynthMode activation now clears ARP/SEQ through
  `runtimeStageNormalizedParameterOnly()` so `paramValues`, `lastAppliedParamValues`,
  and `runtimeModel_` stay under one canonical law.
- Phase2 SynthMode activation now concretely disables ARP backend state via
  `allNotesOff()` and `setEnabled(false)`.
- Phase2 SynthMode activation now applies SEQ cleanup through
  `runtimePolicyHandleSeqEnable(0.0f)`.
- Updated legacy Classic authority guard to require canonical Phase2 staging instead
  of direct `paramValues[]` writes.
- Added `AuthorityStagingPolicyV945Tests`.

## Validation focus

- Authority staging consistency across AU3 and Phase2.
- Hidden ARP/SEQ non-authority under SynthMode/DrSID.
- Source guards for avoiding direct `paramValues[]` bypasses in Phase2 SynthMode policy.
