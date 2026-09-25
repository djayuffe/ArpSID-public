# RELEASE_NOTES_V943 — authority split-brain canonicalization closure

v943 closes the remaining v942 authority split-brain audit findings around raw ARP/SEQ, runtimeModel state-root, and render/runtime parameter synchronization.

## Fixed

- AU3 `enforceComponentFlavorPolicy_()` now stages forced flavor params through `runtimeStageNormalizedParameterOnly()` so `params_`, `renderParams_`, dirty flags, and `runtimeModel_.stateRoot()` remain synchronized.
- AU3 render-mode sanitizer now mirrors sanitized DrSID/SynthMode flags back through canonical staging instead of mutating only `renderParams_`.
- AU3 post-flush canonicalization rejects dormant raw `ArpEnable`/`SeqEnable` whenever the resolved render mode is not BitPerfect.
- AU3 DrSID structural authority now explicitly clears `ArpEnable`, `SeqEnable`, ARP active state, SEQ note/countdown/step state, and dirty ARP/SEQ flags.
- Phase2 `runtimeStageNormalizedParameterOnly()` now updates `runtimeModel_` via `applyAutomationPoint()` just like AU3.
- ARP backend application now uses `sidEffectiveArpAuthorityFromLiveParams()` and refuses to arm the ARP engine under SynthMode/DrSID.
- Forced SynthMode/DrSID ARP/SEQ clears now also apply concrete backend/policy cleanup: ARP all-notes-off/disable plus SEQ policy cleanup.
- AU3 and Phase2 `runtimePolicyHandleSeqEnable()` reject hidden SEQ arming outside CLASSIC / BitPerfect and clear note/countdown/step state.

## Added validation

- `AuthoritySplitBrainClosureV943Tests` validates the source contracts for AU3 flavor staging, sanitizer mirror-back, DrSID structural ARP/SEQ clearing, Phase2 model sync, and ARP/SEQ backend effective-authority rejection.
